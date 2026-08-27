/* 2026-08-21T18:00:57.991605 */
/*
* Copyright (c) 2026 Nordic Semiconductor ASA
* SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
*/
#include "nrf_edgeai_user_model.h"
#include "nrf_edgeai_user_types.h"
#include <nrf_edgeai/nrf_edgeai_platform.h>
#include <nrf_edgeai/rt/private/nrf_edgeai_interfaces.h>
#include <assert.h>

//////////////////////////////////////////////////////////////////////////////
/* Nordic EdgeAI Lab Solution ID and Runtime Version */
#define EDGEAI_LAB_SOLUTION_ID_STR      "95625"
#define EDGEAI_RUNTIME_VERSION_COMBINED 0x00000202

//////////////////////////////////////////////////////////////////////////////
#define INPUT_TYPE                         i16

/** User input features type */
#define INPUT_FEATURE_DATA_TYPE            NRF_EDGEAI_INPUT_I16

/** Number of unique features in the original input sample */
#define INPUT_UNIQ_FEATURES_NUM            6

/** Number of unique features actually used by NN from the original input sample */
#define INPUT_UNIQ_FEATURES_USED_NUM       6

/** Number of input feature samples that should be collected in the input window
 *  feature_sample = 1 * INPUT_UNIQ_FEATURES_NUM
 */
#define INPUT_WINDOW_SIZE                  256

/** Number of input feature samples on that the input window is shifted */
#define INPUT_WINDOW_SHIFT                 256

/** Number of subwindows in input feature window,
* the SUBWINDOW_SIZE = INPUT_WINDOW_SIZE / INPUT_SUBWINDOW_NUM
* if the window size is not divisible by the number of subwindows without a remainder,
* the remainder is added to the last subwindow size */
#define INPUT_SUBWINDOW_NUM                 0

#define INPUT_UNIQUE_SCALES_NUM (sizeof(INPUT_FEATURES_SCALE_MIN) / sizeof(INPUT_FEATURES_SCALE_MIN[0])) 

/** Defines input(also used for LAG) features MIN scaling factor
 */
static const nrf_user_input_t INPUT_FEATURES_SCALE_MIN[] = {
 -32768, -32768, -32768, -31852, -17115, -14912 };

/** Defines input(also used for LAG) features MAX scaling factor
 */
static const nrf_user_input_t INPUT_FEATURES_SCALE_MAX[] = {
 32764, 32764, 32759, 32518, 13368, 12226 };

/** Defines which unique features from the input data will be used/collected,
 *  one bit for one unique feature, starting from LSB
 */
#define INPUT_FEATURES_USAGE_MASK NULL

/** Defines which unique input features is used for LAG features processing,
 *  one bit for one unique feature, starting from LSB
 */
#define INPUT_FEATURES_USED_FOR_LAGS_MASK NULL

//////////////////////////////////////////////////////////////////////////////
#define MODEL_TYPE                 __NRF_EDGEAI_MODEL_NEUTON
#define MODEL_TASK                 0
#define MODEL_OUTPUTS_NUM          8

#define MODEL_USES_AS_INPUT_INPUT_FEATURES 0
#define MODEL_USES_AS_INPUT_DSP_FEATURES 1
#define MODEL_USES_AS_INPUT_MASK ((MODEL_USES_AS_INPUT_INPUT_FEATURES << 0) | (MODEL_USES_AS_INPUT_DSP_FEATURES << 1)) 

#if MODEL_TYPE == __NRF_EDGEAI_MODEL_AXON 
#include <drivers/axon/nrf_axon_nn_infer.h>  
#include <axon/nrf_axon_platform.h> 
#include "nrf_edgeai_user_model_axon.h" 
#define P_MODEL_INSTANCE &model_axon_user_instance_95625
#else  // MODEL_TYPE == __NRF_EDGEAI_MODEL_NEUTON
#define P_MODEL_INSTANCE &model_neuton_user_instance_ 
#endif


#define NN_DECODED_OUTPUT_INIT                 \
.classif = {                                   \
   .predicted_class = 0,                       \
   .num_classes = MODEL_OUTPUTS_NUM,           \
}

//////////////////////////////////////////////////////////////////////////////
#define MODEL_NEURONS_NUM          43
#define MODEL_WEIGHTS_NUM          387
#define MODEL_PARAMS_TYPE          f32
#define MODEL_REORDERING           0

static const nrf_user_weight_t MODEL_WEIGHTS[] = {
 -0.9737392, 0.4630278, 1.0000000, 0.9844497, 0.9871419, 0.5106555,
 -1.0000000, 0.5830661, -0.7383154, 0.5729392, 1.0000000, -0.9752380,
 0.2106538, -0.9895358, 0.1399059, -0.5248304, 0.1400531, -0.0463746,
 -0.5271369, 0.0316595, -0.9868660, 0.9982618, 0.9612056, 0.1709685,
 -0.1018676, 0.6264850, 0.7519736, 0.0522273, 0.5205455, -0.5000000,
 0.7500000, 0.0883298, -0.0775554, 0.5653774, 0.0297116, 0.9133877,
 0.9996346, -0.2912500, -0.1257046, 0.9999992, 0.4142468, -0.7916666,
 1.0000000, -0.5012749, 0.3626249, -0.9004509, -0.4775752, -0.3291259,
 -1.0000000, 0.5000000, -0.4375000, -0.7562258, -0.7384357, 1.0000000,
 0.2669945, 1.0000000, -0.4055559, 0.1369354, -0.0983095, 0.9410094,
 -0.5892794, 1.0000000, 0.0577511, -0.9999995, -0.9272431, 0.4018539,
 -0.0711445, -0.2171920, -0.2539476, -0.1685688, 0.6837959, 0.2418706,
 0.3959861, 0.2837095, -0.2119663, -0.3198681, -0.1492562, -0.0241999,
 -0.5225466, 0.5054726, -0.7616439, 0.1134466, 0.2336778, 0.3120402,
 0.5111470, 0.2077502, 0.0185692, 0.9514227, -0.8026220, -0.3825988,
 -0.9691060, -0.6043866, 0.9999951, -0.5260466, 0.8816275, 0.9973027,
 0.8704624, -0.9999985, -0.9738422, 0.9999995, -0.2745230, 0.9712219,
 -0.2749553, -0.3203682, 0.1644554, 0.2852356, -0.8591908, -0.1118964,
 0.6824943, -0.9274091, 0.3797845, -0.1124497, 1.0000000, -0.5138810,
 -0.3193712, -0.1069633, 1.0000000, -0.5000000, -0.7523177, -0.0655925,
 0.9985352, -1.0000000, 0.5000000, -0.9735599, 0.2322809, 0.6173637,
 -0.6244273, -0.9784014, -1.0000000, 0.0730026, -0.1023870, 0.1654859,
 -1.0000000, -0.3005826, 0.0123671, 0.0440047, 0.8957120, -0.8593750,
 0.0511952, -0.3225682, 1.0000000, 0.2435469, -1.0000000, -0.3290156,
 -0.1149713, -0.3335257, 0.1044938, -0.9249178, -0.0988048, -0.0912862,
 0.2824018, 1.0000000, -1.0000000, -0.1062791, -0.9181899, -0.9050711,
 0.2257040, 0.1628083, 0.9999999, 0.9829736, 0.1640736, 0.1796775,
 -1.0000000, -0.4815413, 0.2620610, 0.1360345, 0.1326695, -0.6852139,
 0.4285489, 1.0000000, -0.5540465, -0.1381041, 0.0223457, -0.1203583,
 1.0000000, 0.9998688, 0.9999995, 1.0000001, -0.3282140, -0.6102923,
 0.4492150, 0.2320373, 0.6335935, -0.2173356, 1.0000000, -0.5000000,
 -0.1022164, -1.0000000, -0.8842946, 0.8461925, -1.0000000, -0.5000000,
 -0.5274659, 0.5000000, 0.1414726, -1.0000000, -0.0287449, -1.0000000,
 -0.8171005, 0.9163314, 0.7331314, 0.1400010, 0.7849660, 0.8808129,
 0.2753934, 0.2727260, -0.7277100, -0.5000000, 1.0000000, 1.0000000,
 0.5000000, -1.0000000, -0.5000000, 0.0000000, 1.0000000, -0.8750000,
 -0.8586121, 0.0976739, 0.9999993, 0.3866352, -0.1853779, 0.5939931,
 0.7500000, 1.0000000, -1.0000000, -0.7499704, 0.8750000, -0.4899557,
 -0.7603742, 0.1696762, 0.0685945, 0.4742808, 0.1992011, 0.0844319,
 -1.0000000, 0.3117312, -0.1320081, 1.0000000, -1.0000000, -1.0000000,
 0.0730506, 0.2478574, 0.6496058, -0.0154556, -0.5472014, 0.9999948,
 0.5404327, 0.9999281, 0.8976622, -0.9813828, -0.5548519, 0.7929118,
 -0.4260646, -0.5004320, -1.0000000, 0.7538888, -0.9980234, -0.0979093,
 0.2615049, -0.5415299, 0.6773131, -0.4216516, 0.0835944, -0.0506105,
 0.3220374, 0.1124446, -0.9968631, 0.0919560, -0.1316126, 0.7988244,
 0.8515625, -0.5068924, -0.4730343, -0.4955308, -0.5892981, -0.8879382,
 0.2887090, -0.6731583, 0.7927709, 0.0213372, -0.1509959, -0.7948706,
 -0.6762008, 1.0000000, -0.7992054, 0.6371332, 0.0389175, -0.1059244,
 0.0865260, 0.8137463, 0.0321727, 0.9999316, -1.0000000, -0.4372761,
 -0.7013736, -1.0000000, 0.8263966, 0.1046703, -0.0047739, 0.0611225,
 1.0000000, 0.0538206, 0.0132086, -0.7685931, 0.3546098, 1.0000000,
 0.0088742, -1.0000000, -0.0952119, 0.0038898, -1.0000000, 0.9957812,
 -0.7392006, -0.2716364, 0.9877930, -1.0000000, 0.8836106, -0.9999369,
 0.3836346, -0.9229358, 0.5000000, -1.0000000, 1.0000000, -1.0000000,
 -0.0802267, -0.9995314, 0.3092920, 0.0686636, -0.0622234, 1.0000000,
 -1.0000000, -1.0000000, 0.9408479, -1.0000000, 1.0000000, -1.0000000,
 -0.9718869, -0.6094462, 0.6089745, -0.2455968, -1.0000000, -0.1375999,
 -0.2832335, -0.3827605, -0.1863391, 0.0102946, 0.3910796, -0.0263870,
 0.0165674, 1.0000000, 1.0000000, -1.0000000, 1.0000000, -1.0000000,
 0.9998779, -0.0847115, 0.2500916, -0.1843103, 0.1562240, 0.6449319,
 -0.8185813, -0.5305721, 0.2702771, 0.9985296, 0.5000000, 0.9980823,
 0.4053127, 0.9976550, -0.5139966, -0.8311077, -1.0000000, -1.0000000,
 0.4146309, 0.9999681, 0.0386992, -0.0031599, -0.7019494, 0.7566364,
 -1.0000000, -1.0000000, 0.8625479, 0.3513998, -1.0000000, -1.0000000,
 0.3482524, -0.9999956, 0.1730213 };

static const uint16_t MODEL_NEURONS_LINKS[] = {
 8, 180, 181, 182, 183, 188, 193, 194, 195, 202, 214, 360, 375, 914, 1080,
 0, 1, 27, 180, 188, 192, 194, 202, 207, 368, 374, 382, 407, 410, 446, 554,
 595, 765, 907, 1080, 8, 14, 34, 38, 54, 70, 153, 181, 188, 231, 349, 363,
 406, 413, 415, 464, 564, 567, 680, 695, 728, 746, 766, 910, 1080, 374,
 543, 576, 723, 771, 1080, 52, 54, 161, 206, 228, 306, 335, 347, 348, 362,
 391, 401, 596, 720, 904, 916, 923, 924, 954, 959, 962, 1056, 1080, 548,
 601, 682, 723, 739, 876, 903, 908, 912, 1080, 5, 1080, 5, 19, 183, 476,
 543, 637, 739, 768, 903, 933, 1080, 7, 1080, 183, 418, 543, 544, 586, 590,
 1080, 9, 1080, 0, 1, 2, 8, 181, 202, 221, 1080, 1, 11, 1, 764, 1080, 2,
 70, 751, 1080, 3, 185, 380, 589, 726, 767, 1080, 4, 766, 924, 1080, 4, 15,
 1080, 2, 11, 0, 8, 372, 593, 1080, 1, 17, 14, 181, 1080, 0, 1, 11, 12, 18,
 39, 48, 407, 494, 512, 524, 562, 586, 749, 924, 1080, 23, 48, 63, 108,
 225, 494, 512, 562, 667, 922, 1080, 0, 1, 18, 23, 48, 108, 188, 221, 225,
 329, 512, 550, 765, 1080, 0, 18, 313, 329, 349, 512, 1080, 17, 18, 22, 23,
 136, 181, 199, 201, 214, 232, 357, 363, 414, 502, 512, 590, 798, 902, 914,
 924, 935, 1080, 1, 11, 19, 23, 185, 201, 383, 947, 1080, 2, 185, 201, 236,
 383, 572, 947, 1080, 1, 24, 8, 155, 407, 767, 925, 947, 1080, 14, 40, 181,
 765, 900, 946, 947, 1080, 14, 19, 26, 8, 40, 181, 185, 188, 194, 211, 368,
 419, 421, 425, 723, 728, 765, 767, 918, 1080, 2, 11, 17, 19, 21, 23, 24,
 46, 363, 1080, 28, 42, 47, 194, 368, 723, 765, 903, 946, 1080, 46, 363,
 1080, 0, 11, 17, 18, 19, 20, 21, 22, 23, 29, 31, 1080, 3, 765, 767, 1080,
 14, 33, 18, 100, 104, 684, 746, 765, 942, 1080, 3, 14, 100, 365, 555, 765,
 942, 1080, 303, 1080, 3, 14, 33, 34, 35, 36, 1080, 28, 42, 211, 385, 572,
 947, 1080, 2, 13, 22, 14, 54, 1080, 2, 13, 39, 1080, 14, 32, 1080, 1, 12,
 24, 25, 26, 27, 28, 30, 38, 41, 1080 };

static const uint16_t MODEL_NEURON_INTERNAL_LINKS_NUM[] = {
 0, 16, 35, 60, 66, 89, 100, 101, 113, 114, 122, 126, 133, 137, 141, 148,
 153, 156, 163, 171, 182, 196, 209, 217, 240, 245, 255, 262, 273, 297, 301,
 310, 324, 326, 331, 341, 347, 355, 357, 366, 372, 374, 386 };

static const uint16_t MODEL_NEURON_EXTERNAL_LINKS_NUM[] = {
 15, 35, 60, 66, 89, 99, 101, 112, 114, 121, 123, 131, 136, 140, 147, 151,
 154, 161, 166, 182, 193, 207, 214, 236, 245, 253, 262, 270, 290, 300, 310,
 313, 325, 329, 339, 347, 349, 356, 363, 369, 373, 376, 387 };

static const nrf_user_coeff_t MODEL_NEURON_ACTIVATION_WEIGHTS[] = {
 40.0000000, 40.0000000, 40.0000000, 40.0000000, 40.0000000, 40.0000000,
 39.9999847, 40.0000000, 40.0000000, 40.0000000, 40.0000000, 39.5330048,
 40.0000000, 39.9999771, 40.0000000, 40.0000000, 40.0000000, 39.5330048,
 39.5330048, 40.0000000, 40.0000000, 40.0000000, 40.0000000, 40.0000000,
 40.0000000, 40.0000000, 40.0000000, 40.0000000, 40.0000000, 40.0000000,
 40.0000000, 40.0000000, 40.0000000, 40.0000000, 40.0000000, 40.0000000,
 40.0000000, 40.0000000, 40.0000000, 40.0000000, 39.9999924, 40.0000000,
 40.0000000 };

static const uint8_t MODEL_NEURON_ACTIVATION_TYPE_MASK[] = {
 0xbf, 0xfa, 0xfe, 0xff, 0xde, 0x2 };

static const uint16_t MODEL_OUTPUT_NEURONS_INDICES[] = {
 32, 42, 40, 37, 16, 6, 8, 10 };

/** Model neurons activations buffer */ 
static nrf_user_neuron_t model_neurons_[MODEL_NEURONS_NUM];

/** Neuton model instance */ 
static const nrf_edgeai_model_neuton_t model_neuton_user_instance_ = { 
   .meta.p_neuron_internal_links_num = MODEL_NEURON_INTERNAL_LINKS_NUM,
   .meta.p_neuron_external_links_num = MODEL_NEURON_EXTERNAL_LINKS_NUM,
   .meta.p_output_neurons_indices    = MODEL_OUTPUT_NEURONS_INDICES,
   .meta.p_neuron_links              = MODEL_NEURONS_LINKS,
   .meta.p_neuron_act_type_mask      = MODEL_NEURON_ACTIVATION_TYPE_MASK,
   .meta.outputs_num                 = MODEL_OUTPUTS_NUM,
   .meta.neurons_num                 = MODEL_NEURONS_NUM,
   .meta.weights_num                 = MODEL_WEIGHTS_NUM,
   /// 
   .params.MODEL_PARAMS_TYPE = {
       .p_weights      = MODEL_WEIGHTS,
       .p_act_weights  = MODEL_NEURON_ACTIVATION_WEIGHTS,
       .p_neurons      = model_neurons_,
   },
};

//////////////////////////////////////////////////////////////////////////////
/** Input feature buffer element size, 
 * if quantization of model is bigger than input features size in bits, 
 * the size of input buffer should aligned to nrf_user_neuron_t */ 
#define INPUT_TYPE_SIZE \
    ((sizeof(nrf_user_input_t) > sizeof(nrf_user_neuron_t)) ? sizeof(nrf_user_input_t) : sizeof(nrf_user_neuron_t)) 

/** Input features window size in bytes to allocate statically */ 
#define INPUT_WINDOW_BUFFER_SIZE_BYTES \
    (INPUT_WINDOW_SIZE * INPUT_UNIQ_FEATURES_NUM * INPUT_TYPE_SIZE) 

static uint8_t input_window_[INPUT_WINDOW_BUFFER_SIZE_BYTES] __NRF_EDGEAI_ALIGNED; 

#define INPUT_WINDOW_MEMORY    &input_window_[0] 

static nrf_edgeai_window_ctx_t input_window_ctx_; 
#define P_INPUT_WINDOW_CTX     &input_window_ctx_ 

//////////////////////////////////////////////////////////////////////////////
/** The maximum number of extracted features that user used for all unique input features */
#define EXTRACTED_FEATURES_NUM  1080

#define EXTRACTED_FEATURES_META_TYPE i32 

/** DSP feature buffer element size,
 * if quantization of model is bigger than DSP features size in bits,
 * the size of extracted DSP features buffer should aligned to nrf_user_neuron_t */
#define EXTRACTED_FEATURE_SIZE_BYTES                                                  \
    ((sizeof(nrf_user_feature_t) > sizeof(nrf_user_neuron_t)) ? sizeof(nrf_user_feature_t) : \
                                                            sizeof(nrf_user_neuron_t))

/** Size of extracted features buffer in bytes */
#define EXTRACTED_FEATURES_BUFFER_SIZE_BYTES (EXTRACTED_FEATURES_NUM * EXTRACTED_FEATURE_SIZE_BYTES) 

/** Defines feature extraction masks used as nrf_edgeai_features_mask_t,
 *  64 bit for one unique input feature, @ref nrf_edgeai_features_mask_t to see bitmask
 */

static const uint64_t FEATURES_EXTRACTION_MASK[] = {
 0xfffffff00001fff, 0xfffffff00001fff, 0xfffffff00001fff,
 0xfffffff00001fff, 0xfffffff00001fff, 0xfffffff00001fff };

/** Defines arguments used while feature extraction
 */

/** Defines arguments used while feature extraction
 */
static const nrf_user_input_t FEATURES_EXTRACTION_ARGUMENTS[] =
{ 0, 4, 4, 2, 2, 2, 2, 128, 0, 4, 4, 2, 2, 2, 2, 128, 0, 4, 4, 2, 2, 2, 2,
 128, 0, 4, 4, 2, 2, 2, 2, 128, 0, 4, 4, 2, 2, 2, 2, 128, 0, 4, 4, 2, 2, 2,
 2, 128 };

/** Defines extracted features MIN scaling factor
 */
static const nrf_user_feature_t EXTRACTED_FEATURES_SCALE_MIN[] = {
 -32768, -9802, 173, -15653, 34, -7922, 4968, 41, 310, 3, 0, 0, 152, 50,
 262, 7, 0, 0, 0, 160, 0, 100, 251, 120, 18, 502, -374, -17949, 1, 1, 1, 3,
 6, 10, 3, 3, 3, 3, 78, 0, 0, 0, 0, 0, 2, -32731, 28, -32732, 3, 358, 11,
 26, 6, 3, 2, 2, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 1, -32768, -9806, 244, -10700, 58, -7216, 5009, 68, 336, 3, 0,
 0, 237, 48, 272, 6, 0, 0, 0, 187, 0, 102, 221, 554, 18, 518, -374, -12304,
 1, 1, 2, 3, 6, 9, 4, 4, 3, 3, 68, 57, 0, 0, 0, 0, 2, -32761, 20, -32739,
 5, 347, 10, 25, 7, 9, 2, 5, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 1, -32768, -4156, 153, -12818, 28, -11715, 4846,
 35, 97, 3, 0, 0, 126, 49, 86, 6, 0, 0, 0, 128, 0, 102, 222, 321, 12, 209,
 -371, -15103, 1, 1, 1, 3, 1, 17, 4, 3, 3, 3, 108, 0, 114, 0, 0, 8, 2,
 -32733, 18, -32709, 3, 301, 10, 26, 6, 6, 2, 1, 1, 1, 0, 1, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -31852, -1262, 79, -7566, 16,
 -10684, 4901, 21, 28, 3, 0, 0, 78, 5, 24, 0, 0, 0, 0, 242, 0, 109, 27,
 835, 20, 3363, -371, -9256, 1, 1, 3, 6, 8, 10, 2, 2, 2, 2, 44, 0, 0, 0, 0,
 0, 2, -32756, 12, -32669, 2, 433, 9, 25, 7, 4, 4, 2, 0, 1, 0, 1, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -17115, -789, 35,
 -4638, 7, -13919, 4845, 9, 9, 3, 0, 0, 32, 4, 7, 0, 0, 0, 0, 222, 0, 105,
 20, 864, 20, 5900, -373, -5273, 1, 1, 3, 5, 7, 3, 2, 2, 2, 2, 89, 0, 0, 0,
 0, 0, 2, -32692, 16, -32712, 2, 366, 10, 24, 6, 2, 0, 0, 0, 0, 0, 1, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -14912, -2106, 28,
 -6069, 5, -9165, 4696, 7, 20, 3, 0, 0, 25, 5, 15, 0, 0, 0, 0, 167, 0, 109,
 22, 916, 19, 7042, -374, -7877, 1, 3, 3, 6, 6, 2, 2, 2, 2, 2, 65, 0, 0, 0,
 0, 3, 2, -32698, 10, -32625, 2, 400, 8, 24, 6, 3, 1, 1, 0, 1, 1, 1, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 };

/** Defines extracted features MAX scaling factor
 */
static const nrf_user_feature_t EXTRACTED_FEATURES_SCALE_MAX[] = {
 9955, 32764, 65512, 19127, 20784, 9288, 38136, 22374, 23369, 109, 70, 70,
 65328, 65385, 22158, 2957, 54, 54, 1000, 871, 156, 376, 65039, 1008, 654,
 306360, 375, 19904, 49, 104, 107, 107, 124, 12247, 6929, 4783, 3134, 2435,
 1232, 3029, 1983, 1944, 1500, 11868, 56, 32630, 535, 32712, 1598, 1130,
 49, 43, 15883, 12247, 8399, 9187, 7109, 5790, 5157, 4614, 5109, 5206,
 3557, 3225, 3685, 3147, 2884, 2558, 2435, 2093, 2637, 2397, 2235, 2129,
 2127, 2375, 2542, 1807, 1834, 1575, 1403, 1814, 1799, 1689, 1378, 1352,
 1595, 1480, 1444, 1203, 1186, 1032, 1268, 1372, 1094, 1151, 1056, 1095,
 1181, 1135, 1416, 1405, 1152, 992, 1060, 1253, 1006, 886, 1140, 1172,
 1016, 713, 736, 914, 700, 677, 748, 919, 829, 625, 660, 769, 844, 812,
 716, 831, 868, 723, 640, 712, 713, 779, 787, 688, 680, 665, 658, 672, 597,
 592, 538, 531, 478, 640, 591, 582, 528, 499, 509, 545, 530, 557, 453, 466,
 520, 481, 479, 460, 436, 468, 400, 460, 508, 462, 492, 625, 560, 554, 442,
 534, 559, 564, 478, 591, 520, 458, 519, 578, 525, 618, 4989, 32764, 65493,
 13095, 15606, 10404, 43992, 18988, 19391, 70, 50, 50, 65031, 65361, 15787,
 2836, 50, 50, 1000, 800, 113, 406, 65006, 1007, 524, 78500, 371, 14055,
 11, 27, 107, 112, 123, 11129, 5058, 3586, 2960, 2357, 1206, 3246, 2072,
 2016, 1637, 10852, 57, 32764, 545, 32757, 1184, 1135, 43, 44, 6903, 11129,
 6768, 8814, 5922, 4704, 4980, 3701, 4425, 3386, 2623, 2625, 2784, 2781,
 2664, 2085, 2109, 2053, 1867, 1789, 1832, 2088, 2096, 1862, 1693, 1726,
 1782, 1657, 1577, 1610, 1400, 1286, 1531, 1375, 1129, 1296, 1256, 1092,
 1232, 1239, 1340, 957, 1121, 1055, 1152, 944, 897, 997, 773, 886, 1135,
 973, 807, 1035, 848, 969, 932, 806, 697, 846, 799, 880, 864, 852, 641,
 646, 751, 783, 830, 777, 634, 678, 602, 687, 640, 635, 586, 638, 712, 540,
 645, 704, 620, 664, 625, 562, 570, 545, 623, 619, 533, 504, 554, 468, 408,
 540, 413, 471, 377, 519, 458, 462, 463, 351, 446, 345, 418, 345, 399, 424,
 442, 443, 564, 424, 488, 512, 419, 453, 514, 451, 452, 444, 504, 597, 474,
 498, 483, 610, 7671, 32759, 65479, 8651, 15785, 14448, 69864, 18879,
 19368, 105, 43, 43, 65291, 65155, 16100, 1828, 39, 39, 1000, 820, 109,
 515, 62650, 1007, 774, 118250, 374, 10008, 12, 105, 60, 105, 111, 11000,
 4820, 2980, 2663, 2215, 1214, 2101, 2089, 1336, 2456, 15323, 95, 32762,
 611, 32764, 1200, 1137, 48, 43, 7434, 11000, 7003, 7818, 6291, 4197, 3626,
 3619, 2727, 2385, 2916, 2232, 2669, 2867, 2485, 1852, 2215, 2138, 1295,
 1243, 1313, 1168, 1152, 1604, 1106, 953, 990, 1189, 1492, 1764, 1858,
 1847, 1766, 1571, 1310, 1035, 917, 988, 855, 1013, 1130, 865, 737, 898,
 642, 591, 613, 651, 668, 736, 700, 795, 868, 860, 775, 809, 636, 565, 514,
 472, 379, 455, 477, 466, 507, 528, 566, 517, 407, 517, 554, 422, 390, 467,
 539, 598, 614, 585, 517, 421, 400, 555, 519, 359, 382, 348, 336, 326, 375,
 326, 414, 380, 448, 525, 450, 415, 498, 385, 377, 324, 322, 321, 327, 305,
 296, 264, 261, 432, 335, 304, 400, 313, 298, 347, 358, 369, 373, 402, 426,
 420, 401, 361, 481, 442, 301, 299, 315, 285, 1701, 32518, 64370, 7172,
 9076, 7651, 41622, 13804, 13816, 31, 27, 27, 62887, 63652, 8967, 823, 15,
 15, 1000, 777, 117, 349, 13568, 1026, 286, 71428, 374, 8823, 21, 123, 125,
 125, 125, 5980, 1930, 1251, 1204, 811, 1218, 2423, 1500, 1500, 2000,
 11581, 95, 32658, 323, 32722, 863, 1157, 51, 46, 521, 5980, 2245, 3294,
 3595, 3614, 3743, 3341, 2317, 1154, 574, 395, 353, 864, 1204, 1189, 1146,
 1251, 1208, 888, 517, 345, 193, 180, 492, 634, 642, 714, 811, 746, 537,
 369, 288, 134, 132, 323, 392, 428, 535, 604, 533, 391, 326, 273, 134, 119,
 196, 253, 350, 492, 532, 453, 338, 271, 189, 81, 89, 142, 186, 287, 380,
 384, 328, 302, 285, 206, 69, 60, 103, 164, 273, 338, 324, 289, 291, 269,
 171, 62, 60, 69, 154, 255, 294, 275, 266, 277, 240, 147, 70, 65, 70, 148,
 234, 253, 236, 256, 291, 265, 187, 92, 87, 126, 231, 276, 227, 188, 206,
 209, 162, 113, 109, 63, 57, 138, 179, 187, 223, 269, 256, 192, 143, 118,
 59, 53, 135, 168, 189, 243, 992, 13368, 20400, 4739, 5103, 8577, 70200,
 5735, 6549, 39, 35, 35, 20246, 3429, 4739, 135, 23, 23, 1000, 796, 121,
 564, 7562, 1045, 240, 79333, 374, 6814, 8, 125, 125, 125, 126, 3478, 1700,
 576, 396, 231, 1226, 1826, 1500, 1500, 1500, 15458, 75, 32754, 318, 32746,
 359, 1162, 58, 46, 12, 3478, 1723, 1700, 1061, 779, 576, 443, 397, 364,
 302, 279, 242, 218, 197, 191, 177, 160, 156, 141, 139, 129, 122, 122, 115,
 110, 102, 103, 98, 98, 91, 87, 85, 84, 85, 78, 78, 74, 77, 76, 71, 71, 64,
 69, 72, 64, 62, 68, 62, 61, 67, 62, 56, 61, 55, 52, 54, 57, 54, 58, 50,
 47, 55, 49, 47, 45, 46, 47, 48, 44, 43, 44, 44, 43, 42, 42, 40, 41, 42,
 41, 40, 36, 39, 40, 41, 41, 41, 42, 39, 43, 43, 41, 39, 40, 40, 40, 38,
 35, 39, 46, 41, 60, 38, 45, 50, 48, 41, 52, 49, 49, 38, 37, 37, 38, 38,
 34, 37, 35, 37, 32, 34, 32, 35, 35, 35, 35, 33, 36, 1655, 12226, 17965,
 5684, 4247, 8116, 33172, 4694, 7621, 35, 31, 31, 17556, 1645, 6069, 167,
 15, 15, 1000, 753, 125, 419, 6190, 1024, 183, 84210, 374, 8189, 25, 123,
 125, 125, 125, 3223, 906, 319, 144, 99, 1226, 1500, 1500, 1500, 1500,
 21542, 78, 32696, 244, 32703, 293, 1150, 56, 46, 12, 3223, 2200, 1186,
 906, 759, 462, 260, 246, 222, 209, 187, 165, 148, 137, 122, 116, 112, 106,
 103, 102, 96, 89, 79, 75, 71, 70, 66, 65, 63, 63, 60, 56, 55, 53, 51, 50,
 49, 49, 47, 47, 47, 46, 55, 49, 47, 52, 45, 39, 40, 36, 38, 37, 36, 34,
 34, 34, 34, 34, 40, 38, 36, 34, 35, 30, 31, 31, 29, 29, 29, 28, 30, 27,
 28, 30, 31, 27, 27, 27, 28, 26, 27, 27, 27, 27, 28, 27, 31, 32, 30, 29,
 29, 29, 26, 26, 26, 26, 28, 31, 45, 44, 30, 33, 46, 51, 36, 36, 42, 36,
 31, 28, 25, 23, 22, 23, 23, 24, 25, 25, 24, 23, 22, 24, 25, 23, 24, 24,
 25 };

/** Memory allocation to store extracted features during DSP pipeline */
static uint8_t extracted_features_buffer_[EXTRACTED_FEATURES_BUFFER_SIZE_BYTES] __NRF_EDGEAI_ALIGNED;


/** Timedomain features processing context  */
#define P_TIMEDOMAIN_FEATURES_CTX  NULL
/** Timedomain features in feature extraction pipeline  */
static const nrf_edgeai_features_pipeline_func_i16_t timedomain_features_[] = {
    nrf_edgeai_feature_utility_tss_sum_i16,
    nrf_edgeai_feature_min_max_range_i16,
    nrf_edgeai_feature_mean_i16,
    nrf_edgeai_feature_mad_i16,
    nrf_edgeai_feature_skew_kur_i16,
    nrf_edgeai_feature_std_i16,
    nrf_edgeai_feature_rms_i16,
    nrf_edgeai_feature_mcr_i16,
    nrf_edgeai_feature_zcr_i16,
    nrf_edgeai_feature_tcr_i16,
    nrf_edgeai_feature_p2p_lf_hf_i16,
    nrf_edgeai_feature_absmean_i16,
    nrf_edgeai_feature_amdf_i16,
    nrf_edgeai_feature_pscr_i16,
    nrf_edgeai_feature_nscr_i16,
    nrf_edgeai_feature_psoz_i16,
    nrf_edgeai_feature_psom_i16,
    nrf_edgeai_feature_psos_i16,
    nrf_edgeai_feature_crest_i16,
    nrf_edgeai_feature_rmds_i16,
    nrf_edgeai_feature_autocorr_i16,
    nrf_edgeai_feature_hjorth_i16,
    nrf_edgeai_feature_lrp_i16
 };

static const nrf_edgeai_features_pipeline_ctx_t timedomain_pipeline_ = {
    .functions_num     = sizeof(timedomain_features_) / sizeof(timedomain_features_[0]),
    .functions.p_void  = timedomain_features_,
    .p_ctx             = P_TIMEDOMAIN_FEATURES_CTX,
};
#define P_TIMEDOMAIN_PIPELINE &timedomain_pipeline_ 

/** DSP Amplitude spectrum and complex RFFT length */
#define DSP_AMPLITUDE_SPECTRUM_LEN     128
#define DSP_RFFT_LEN                   (DSP_AMPLITUDE_SPECTRUM_LEN * 2)

/** Defines DSP Complex FFT reverse bit index table length
 */
#define DSP_CFFT_BITREV_INDEX_TABLE_LEN 112

/** Defines DSP Complex FFT reverse bit index table
 */
static const uint16_t DSP_CFFT_BITREV_INDEX_TABLE[] =
{ 8, 512, 16, 256, 24, 768, 32, 128, 40, 640, 48, 384, 56, 896, 72, 576, 80,
 320, 88, 832, 96, 192, 104, 704, 112, 448, 120, 960, 136, 544, 144, 288,
 152, 800, 168, 672, 176, 416, 184, 928, 200, 608, 208, 352, 216, 864, 232,
 736, 240, 480, 248, 992, 264, 528, 280, 784, 296, 656, 304, 400, 312, 912,
 328, 592, 344, 848, 360, 720, 368, 464, 376, 976, 392, 560, 408, 816, 424,
 688, 440, 944, 456, 624, 472, 880, 488, 752, 504, 1008, 536, 776, 552,
 648, 568, 904, 600, 840, 616, 712, 632, 968, 664, 808, 696, 936, 728, 872,
 760, 1000, 824, 920, 888, 984 };

/** Defines DSP Real FFT twiddle factors table
 */
static const nrf_user_input_t DSP_RFFT_TWIDDLE_FACTORS[] =
{ 16384, -16384, 15981, -16379, 15580, -16364, 15178, -16339, 14778, -16305,
 14378, -16260, 13980, -16206, 13583, -16142, 13187, -16069, 12794, -15985,
 12403, -15893, 12014, -15790, 11628, -15678, 11244, -15557, 10864, -15426,
 10487, -15286, 10114, -15136, 9744, -14978, 9379, -14811, 9017, -14634,
 8660, -14449, 8308, -14255, 7961, -14053, 7618, -13842, 7281, -13622,
 6950, -13395, 6624, -13159, 6304, -12916, 5990, -12665, 5682, -12406,
 5381, -12139, 5086, -11866, 4799, -11585, 4518, -11297, 4244, -11002,
 3978, -10701, 3719, -10394, 3468, -10080, 3224, -9760, 2989, -9434, 2761,
 -9102, 2542, -8765, 2331, -8423, 2128, -8075, 1935, -7723, 1749, -7366,
 1573, -7005, 1406, -6639, 1247, -6270, 1098, -5896, 958, -5519, 827,
 -5139, 705, -4756, 593, -4370, 491, -3981, 398, -3590, 315, -3196, 241,
 -2801, 177, -2404, 123, -2006, 79, -1606, 44, -1205, 20, -804, 5, -402, 0,
 0, 5, 402, 20, 804, 44, 1205, 79, 1606, 123, 2006, 177, 2404, 241, 2801,
 315, 3196, 398, 3590, 491, 3981, 593, 4370, 705, 4756, 827, 5139, 958,
 5519, 1098, 5896, 1247, 6270, 1406, 6639, 1573, 7005, 1749, 7366, 1935,
 7723, 2128, 8075, 2331, 8423, 2542, 8765, 2761, 9102, 2989, 9434, 3224,
 9760, 3468, 10080, 3719, 10394, 3978, 10701, 4244, 11002, 4518, 11297,
 4799, 11585, 5086, 11866, 5381, 12139, 5682, 12406, 5990, 12665, 6304,
 12916, 6624, 13159, 6950, 13395, 7281, 13622, 7618, 13842, 7961, 14053,
 8308, 14255, 8660, 14449, 9017, 14634, 9379, 14811, 9744, 14978, 10114,
 15136, 10487, 15286, 10864, 15426, 11244, 15557, 11628, 15678, 12014,
 15790, 12403, 15893, 12794, 15985, 13187, 16069, 13583, 16142, 13980,
 16206, 14378, 16260, 14778, 16305, 15178, 16339, 15580, 16364, 15981,
 16379 };

/** Defines DSP Complex FFT twiddle factors table
 */
static const nrf_user_input_t DSP_CFFT_TWIDDLE_FACTORS[] =
{ 32767, 0, 32728, 1608, 32609, 3212, 32412, 4808, 32137, 6393, 31785, 7962,
 31356, 9512, 30852, 11039, 30273, 12539, 29621, 14010, 28898, 15446,
 28105, 16846, 27245, 18204, 26319, 19519, 25329, 20787, 24279, 22005,
 23170, 23170, 22005, 24279, 20787, 25329, 19519, 26319, 18204, 27245,
 16846, 28105, 15446, 28898, 14010, 29621, 12539, 30273, 11039, 30852,
 9512, 31356, 7962, 31785, 6393, 32137, 4808, 32412, 3212, 32609, 1608,
 32728, 0, 32767, -1608, 32728, -3212, 32609, -4808, 32412, -6393, 32137,
 -7962, 31785, -9512, 31356, -11039, 30852, -12539, 30273, -14010, 29621,
 -15446, 28898, -16846, 28105, -18204, 27245, -19519, 26319, -20787, 25329,
 -22005, 24279, -23170, 23170, -24279, 22005, -25329, 20787, -26319, 19519,
 -27245, 18204, -28105, 16846, -28898, 15446, -29621, 14010, -30273, 12539,
 -30852, 11039, -31356, 9512, -31785, 7962, -32137, 6393, -32412, 4808,
 -32609, 3212, -32728, 1608, -32767, 0, -32728, -1608, -32609, -3212,
 -32412, -4808, -32137, -6393, -31785, -7962, -31356, -9512, -30852,
 -11039, -30273, -12539, -29621, -14010, -28898, -15446, -28105, -16846,
 -27245, -18204, -26319, -19519, -25329, -20787, -24279, -22005, -23170,
 -23170, -22005, -24279, -20787, -25329, -19519, -26319, -18204, -27245,
 -16846, -28105, -15446, -28898, -14010, -29621, -12539, -30273, -11039,
 -30852, -9512, -31356, -7962, -31785, -6393, -32137, -4808, -32412, -3212,
 -32609, -1608, -32728 };
/** DSP FFT context */
static nrf_edgeai_features_freq_fft_ctx_t dsp_fft_ctx_ = {
    .INPUT_TYPE = {
        .p_rfft_buffer          = NULL,
        .p_rfft_twiddle_table   = DSP_RFFT_TWIDDLE_FACTORS,
        .p_cfft_twiddle_table   = DSP_CFFT_TWIDDLE_FACTORS,
        .p_cfft_bitrev_table    = DSP_CFFT_BITREV_INDEX_TABLE,
        .cfft_bitrev_table_len  = DSP_CFFT_BITREV_INDEX_TABLE_LEN,
        .rfft_len               = DSP_RFFT_LEN,
    }
};

/** Frequency domain features processing context  */
#define P_FREQDOMAIN_FEATURES_CTX   &dsp_fft_ctx_
/** Frequency domain features in feature extraction pipeline  */
static const nrf_edgeai_features_pipeline_func_i16_t freqdomain_features_[] = {
    nrf_edgeai_feature_utility_rfft_256_i16,
    nrf_edgeai_feature_dom_freqs_features_i16,
    nrf_edgeai_feature_freqs_energy_ratios_i16,
    nrf_edgeai_feature_spectral_rms_i16,
    nrf_edgeai_feature_spectral_crest_i16,
    nrf_edgeai_feature_spectral_centroid_i16,
    nrf_edgeai_feature_spectral_spread_i16,
    nrf_edgeai_feature_spectrum_bins_i16
 };

static const nrf_edgeai_features_pipeline_ctx_t freqdomain_pipeline_ = {
    .functions_num     = sizeof(freqdomain_features_) / sizeof(freqdomain_features_[0]),
    .functions.p_void  = freqdomain_features_,
    .p_ctx             = P_FREQDOMAIN_FEATURES_CTX,
};
#define P_FREQDOMAIN_PIPELINE &freqdomain_pipeline_ 

#define P_CUSTOMDOMAIN_PIPELINE NULL

static nrf_edgeai_dsp_pipeline_t dsp_pipeline_ = { 
   .features = {  
       .p_masks = (const nrf_edgeai_features_mask_t*)FEATURES_EXTRACTION_MASK, 
       .buffer.p_void = extracted_features_buffer_, 
       .overall_num = EXTRACTED_FEATURES_NUM, 
       .masks_num = sizeof(FEATURES_EXTRACTION_MASK) / sizeof(FEATURES_EXTRACTION_MASK[0]), 

       .p_timedomain_pipeline = P_TIMEDOMAIN_PIPELINE, 
       .p_freqdomain_pipeline = P_FREQDOMAIN_PIPELINE, 
       .p_customdomain_pipeline = P_CUSTOMDOMAIN_PIPELINE, 

       .meta.EXTRACTED_FEATURES_META_TYPE = { 
           .p_min = EXTRACTED_FEATURES_SCALE_MIN, 
           .p_max = EXTRACTED_FEATURES_SCALE_MAX, 
       .p_arguments = FEATURES_EXTRACTION_ARGUMENTS, 
       },
   }, 
}; 

#define P_DSP_PIPELINE         &dsp_pipeline_ 


//////////////////////////////////////////////////////////////////////////////
#define NN_INPUT_INIT_INTERFACE        nrf_edgeai_input_init_discrete_window 
#define NN_INPUT_FEED_INTERFACE        nrf_edgeai_input_feed_discrete_window_i16 
#define NN_PROCESS_FEATURES_INTERFACE  nrf_edgeai_process_features_dsp_i16_f32 
#define NN_INIT_INFERENCE_INTERFACE    nrf_edgeai_init_inference_neuton 
#define NN_RUN_INFERENCE_INTERFACE     nrf_edgeai_run_inference_neuton_f32 
#define NN_PROPAGATE_OUTPUTS_INTERFACE nrf_edgeai_output_propagate_neuton_f32 
#define NN_DECODE_OUTPUTS_INTERFACE    nrf_edgeai_output_decode_classification_f32 

//////////////////////////////////////////////////////////////////////////////

static nrf_user_output_t model_outputs_[MODEL_OUTPUTS_NUM];

//////////////////////////////////////////////////////////////////////////////

static nrf_edgeai_t nrf_edgeai_ = {
    ///
    .metadata.p_solution_id     = EDGEAI_LAB_SOLUTION_ID_STR,
    .metadata.version.combined  = EDGEAI_RUNTIME_VERSION_COMBINED,
    ///   
    .input.p_used_for_lags_mask = INPUT_FEATURES_USED_FOR_LAGS_MASK,
    .input.p_usage_mask         = INPUT_FEATURES_USAGE_MASK,
    .input.type                 = INPUT_FEATURE_DATA_TYPE,
    .input.unique_num           = INPUT_UNIQ_FEATURES_NUM,
    .input.unique_num_used      = INPUT_UNIQ_FEATURES_USED_NUM,
    .input.unique_scales_num    = INPUT_UNIQUE_SCALES_NUM,
    .input.window_size          = INPUT_WINDOW_SIZE,
    .input.window_shift         = INPUT_WINDOW_SHIFT,
    .input.subwindow_num        = INPUT_SUBWINDOW_NUM,
    .input.window_memory.p_void = INPUT_WINDOW_MEMORY,
    .input.p_window_ctx         = P_INPUT_WINDOW_CTX,

    .input.scale.INPUT_TYPE = {
        .p_min = INPUT_FEATURES_SCALE_MIN,
        .p_max = INPUT_FEATURES_SCALE_MAX,
    }, 
    ///
    .p_dsp = P_DSP_PIPELINE,
    ///
    .model.type                 = (nrf_edgeai_model_type_t)MODEL_TYPE,
    .model.task                 = (nrf_edgeai_model_task_t)MODEL_TASK,
    .model.instance.p_void      = P_MODEL_INSTANCE,
    .model.output.memory.p_void = model_outputs_,
    .model.output.num           = MODEL_OUTPUTS_NUM,
    .model.uses_as_input.all    = MODEL_USES_AS_INPUT_MASK,
    ///
    .interfaces.input_init          = NN_INPUT_INIT_INTERFACE,
    .interfaces.feed_inputs         = NN_INPUT_FEED_INTERFACE,
    .interfaces.process_features    = NN_PROCESS_FEATURES_INTERFACE,
    .interfaces.init_inference      = NN_INIT_INFERENCE_INTERFACE,
    .interfaces.run_inference       = NN_RUN_INFERENCE_INTERFACE,
    .interfaces.propagate_outputs   = NN_PROPAGATE_OUTPUTS_INTERFACE,
    .interfaces.decode_outputs      = NN_DECODE_OUTPUTS_INTERFACE,
    ///
    .decoded_output = { NN_DECODED_OUTPUT_INIT },
};

//////////////////////////////////////////////////////////////////////////////

nrf_edgeai_t* nrf_edgeai_user_model_95625(void)
{
    return &nrf_edgeai_;
}

//////////////////////////////////////////////////////////////////////////////

uint32_t nrf_edgeai_user_model_size_95625(void)
{
    uint32_t model_size = 0;

#if MODEL_TYPE == __NRF_EDGEAI_MODEL_NEUTON
    model_size +=
        (sizeof(MODEL_WEIGHTS) + sizeof(MODEL_NEURONS_LINKS) +
         sizeof(MODEL_NEURON_EXTERNAL_LINKS_NUM) + sizeof(MODEL_NEURON_INTERNAL_LINKS_NUM) +
         sizeof(MODEL_NEURON_ACTIVATION_WEIGHTS) + sizeof(MODEL_NEURON_ACTIVATION_TYPE_MASK) +
         sizeof(MODEL_OUTPUT_NEURONS_INDICES));

#if MODEL_TASK == __NRF_EDGEAI_TASK_ANOMALY_DETECTION
    model_size += sizeof(MODEL_AVERAGE_EMBEDDING) + sizeof(MODEL_OUTPUT_SCALE_MIN) +
                  sizeof(MODEL_OUTPUT_SCALE_MAX);
#endif

#if MODEL_TASK == __NRF_EDGEAI_TASK_REGRESSION
    model_size += sizeof(MODEL_OUTPUT_SCALE_MIN) + sizeof(MODEL_OUTPUT_SCALE_MAX);
#endif

#elif MODEL_TYPE == __NRF_EDGEAI_MODEL_AXON
    const nrf_axon_nn_compiled_model_s* p_axon_model = P_MODEL_INSTANCE;

    model_size += sizeof(*p_axon_model);
    model_size += p_axon_model->model_const_size;
    model_size += p_axon_model->cmd_buffer_len * sizeof(NRF_AXON_PLATFORM_BITWIDTH_UNSIGNED_TYPE);

    if (p_axon_model->persistent_vars.buf_ptr != NULL)
    {
        model_size +=
            sizeof(nrf_axon_nn_model_persistent_var_s) * p_axon_model->persistent_vars.count;
    }

#endif

    return model_size;
}


