

#include "nn_operator.h"
#include "../nn_op_helper.h"
// #include "nn_op_structs.h"

#include "xs3_vpu.h"

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>


typedef struct {
    int top;
    int left;
    int bottom;
    int right;
} incl_bounds_t;

/**
 * Compute the bounds of the convolution window (in an input image's coordinate space) corresponding to a given 
 * pixel location in the output image's coordinate space.
 * 
 * The bottom and right coordinates are inclusive.
 */
static incl_bounds_t inverse_map(
    const nn_window_params_t* conv_window,
    const int out_row,
    const int out_col)
{
    incl_bounds_t res;
    res.top    = conv_window->start.row    + conv_window->stride.vertical   * out_row;
    res.left   = conv_window->start.column + conv_window->stride.horizontal * out_col;
    res.bottom = res.top  + conv_window->shape.height - 1; //inclusive bottom
    res.right  = res.left + conv_window->shape.width  - 1; //inclusive right
    return res;
}



#ifndef CONV2D_INIT_ERROR_DETECTION_ENABLE
  #define CONV2D_INIT_ERROR_DETECTION_ENABLE     (1)
#endif
void conv2d_deep_init(
    nn_conv2d_deep_plan_t* plan,
    nn_conv2d_deep_job_t* jobs,
    const nn_image_params_t* x_params,
    const nn_image_params_t* y_params,
    const nn_conv2d_job_params_t* job_params,
    const nn_window_params_t* conv_window,
    const int8_t zero_point,
    const unsigned job_count)
{

    if(CONV2D_INIT_ERROR_DETECTION_ENABLE){
        assert(x_params->channels % 4 == 0); //Input channel count must be multiple of 4.
        assert(y_params->channels % 4 == 0); //Output channel count must be multiple of 4.
        assert(job_count > 0); // At least one job must be specified.
        assert(job_count == 1 || job_params != NULL); // job_params may only be NULL if there is exactly 1 job.
    }

    const unsigned x_row_bytes = x_params->width * x_params->channels;
    const unsigned y_row_bytes = y_params->width * y_params->channels;
    const unsigned patch_width_bytes = conv_window->shape.width * x_params->channels;

    const mem_stride_t window_start_offset = conv_window->start.row * x_row_bytes 
                                           + conv_window->start.column * x_params->channels;

    plan->channels.X = x_params->channels;
    plan->channels.Y = y_params->channels;

    plan->zero_point = zero_point;

    plan->window.shape.height  = conv_window->shape.height;
    plan->window.shape.width   = conv_window->shape.width;
    plan->window.stride.vertical = conv_window->stride.vertical;
    plan->window.stride.horizontal = conv_window->stride.horizontal;

    plan->stride.X.row = x_row_bytes - patch_width_bytes;
    // plan->stride.window.col = conv_window->stride.horizontal * x_params->channels;

    plan->stride.K.cout = conv_window->shape.height * conv_window->shape.width * x_params->channels;
    
    const int32_t init_padding_top    = -conv_window->start.row;
    const int32_t init_padding_bottom =  conv_window->start.row + conv_window->shape.height - x_params->height;
    const int32_t init_padding_left   = -conv_window->start.column;
    const int32_t init_padding_right  =  conv_window->start.column + conv_window->shape.width - x_params->width;

    // Job that computes the entire output image (in the output image's coordinates!)
    nn_conv2d_job_params_t full_job = {{0,0,0}, {y_params->height, y_params->width, y_params->channels} };

    for(int i = 0; i < job_count; i++){
        const nn_conv2d_job_params_t* params = (job_params != NULL)? &job_params[i] : &full_job;
        nn_conv2d_deep_job_t* job = &jobs[i];

        if (CONV2D_INIT_ERROR_DETECTION_ENABLE){
            // Start can never be negative
            assert(params->start.rows >= 0); // Job start row must not be negative.
            assert(params->start.cols >= 0); // Job start column must not be negative.
            assert(params->start.channels >= 0); //Job start channel must not be negative.

            assert(params->start.channels % VPU_INT8_ACC_PERIOD == 0); // Job start channel must be multiple of 16.

            // Make sure we're not trying to compute outputs that go beyond
            //  the bounds of the output image.
            assert(params->start.rows + params->size.rows <= y_params->height); // Job extends beyond bottom of output.
            assert(params->start.cols + params->size.cols <= y_params->width ); // Job extends beyond right of output.
            assert(params->start.channels + params->size.channels <= y_params->channels); //Job extends beyond channels of output.

            // Make sure the convolution window is never entirely outside the input image.
            //   (If it is, it would have to also be for the first and/or last pixel)
            {
                int first_row_y = params->start.rows;
                int final_row_y = first_row_y + params->size.rows - 1;
                int first_col_y = params->start.cols;
                int final_col_y = first_col_y + params->size.cols - 1;

                incl_bounds_t bounds = inverse_map(conv_window, first_row_y, first_col_y);

                assert(bounds.bottom >= 0);
                assert(bounds.right >= 0);

                bounds = inverse_map(conv_window, final_row_y, final_col_y);

                assert(bounds.top  < ((int)x_params->height));
                assert(bounds.left < ((int)x_params->width));
            }
        }


        job->output.rows = params->size.rows;
        job->output.cols = params->size.cols;
        job->output.channels = params->size.channels;

        job->init_padding.top    = init_padding_top    - params->start.rows * plan->window.stride.vertical;
        job->init_padding.left   = init_padding_left   - params->start.cols * plan->window.stride.horizontal;
        job->init_padding.bottom = init_padding_bottom + params->start.rows * plan->window.stride.vertical;
        job->init_padding.right  = init_padding_right  + params->start.cols * plan->window.stride.horizontal;

        job->stride.start.BSO  = (params->start.channels / VPU_INT8_ACC_PERIOD);
        job->stride.start.K    = params->start.channels * plan->stride.K.cout;
        job->stride.start.Y    = params->start.rows * y_row_bytes 
                               + params->start.cols * y_params->channels
                               + params->start.channels;

        job->stride.start.X    = window_start_offset 
                               + params->start.rows * plan->window.stride.vertical * x_row_bytes
                               + params->start.cols * plan->window.stride.horizontal * x_params->channels;

        job->stride.row.window  = x_row_bytes * plan->window.stride.vertical;
        job->stride.row.Y       = y_row_bytes;

        job->stride.chan_group.Y = VPU_INT8_ACC_PERIOD - y_row_bytes * job->output.rows;
    }
}




void conv2d_deep(
    nn_image_t* Y,
    const nn_image_t* X,
    const nn_tensor_t* K,
    const nn_bso_block_t* BSO,
    const nn_conv2d_deep_plan_t* plan,
    const nn_conv2d_deep_job_t* job)
{ 
    int8_t zero_point_vec[VPU_INT8_EPV];
    memset(zero_point_vec, plan->zero_point, sizeof(zero_point_vec));
    
    X = ADDR(X,job->stride.start.X);
    Y = ADDR(Y, job->stride.start.Y);
    K = ADDR(K, job->stride.start.K);
    BSO = ADDR(BSO, job->stride.start.BSO);

    const unsigned C_out_tail = plan->channels.Y % VPU_INT8_ACC_PERIOD;

    for(int out_chan = 0; out_chan < job->output.channels; out_chan += VPU_INT8_ACC_PERIOD){

        const unsigned cur_chans = (job->output.channels - out_chan >= VPU_INT8_VLMACC_ELMS)? VPU_INT8_VLMACC_ELMS : job->output.channels - out_chan; 

        int pad_t = job->init_padding.top;
        int pad_b = job->init_padding.bottom;

        K = ADDR(K, plan->stride.K.cout * (cur_chans - 1));

        const nn_image_t* X_cog = X;

        for(int out_row = 0; out_row < job->output.rows; out_row++){

            int pad_l = job->init_padding.left;
            int pad_r = job->init_padding.right;

            const int pad_lr_delta = plan->window.stride.horizontal * (job->output.cols - 1);
            const int final_pad_l = pad_l - pad_lr_delta;
            const int final_pad_r = pad_r + pad_lr_delta;

            const int cur_pad_t = (pad_t > 0)? pad_t : 0;
            const int cur_pad_b = (pad_b > 0)? pad_b : 0;

            const unsigned requires_padding = (pad_l > 0)       || (pad_r > 0) 
                                           || (cur_pad_t > 0)   || (cur_pad_b > 0) 
                                           || (final_pad_l > 0) || (final_pad_r > 0);

            if(cur_chans == VPU_INT8_ACC_PERIOD){
                if(requires_padding){
                    nn_conv2d_hstrip_deep_padded( Y, X_cog, K, BSO, plan->window.shape.height, plan->window.shape.width, 
                        plan->window.stride.horizontal, plan->channels.X, cur_pad_t, cur_pad_b, pad_l, pad_r, 
                        plan->stride.X.row, -plan->stride.K.cout, plan->channels.Y, job->output.cols, zero_point_vec);
                } else {
                    nn_conv2d_hstrip_deep( Y, X_cog, K, BSO, plan->window.shape.height, plan->window.shape.width, 
                        plan->window.stride.horizontal, plan->channels.X, plan->stride.X.row, -plan->stride.K.cout, 
                        plan->channels.Y, job->output.cols);
                }
            } else {
                if(requires_padding){
                    nn_conv2d_hstrip_tail_deep_padded( Y, X_cog, K, BSO, plan->window.shape.height, plan->window.shape.width, 
                        plan->window.stride.horizontal, plan->channels.X, cur_pad_t, cur_pad_b, pad_l, pad_r, 
                        plan->stride.X.row, -plan->stride.K.cout, plan->channels.Y, job->output.cols, 
                        zero_point_vec, C_out_tail);
                } else {
                    nn_conv2d_hstrip_tail_deep( Y, X_cog, K, BSO, plan->window.shape.height, plan->window.shape.width, 
                        plan->window.stride.horizontal, plan->channels.X, plan->stride.X.row, -plan->stride.K.cout, 
                        plan->channels.Y, job->output.cols, C_out_tail);
                }
            }

            pad_t -= plan->window.stride.vertical;
            pad_b += plan->window.stride.vertical;

            X_cog = ADDR(X_cog, job->stride.row.window);
            Y = ADDR(Y, job->stride.row.Y);
        }

        K = ADDR(K, plan->stride.K.cout);
        Y = ADDR(Y, job->stride.chan_group.Y);
        BSO = ADDR(BSO, 1);
    }
}