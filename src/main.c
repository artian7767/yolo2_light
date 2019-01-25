#include <stdio.h>
#include <stdlib.h>

#include "box.h"
#include "pthread.h"

#include "additionally.h"
#include "http_stream.h"
#ifdef OPENCV
#include "opencv2/highgui/highgui_c.h"
#include "opencv2/core/core_c.h"
#include "opencv2/core/version.hpp"
#ifndef CV_VERSION_EPOCH
#include "opencv2/videoio/videoio_c.h"
#define OPENCV_VERSION CVAUX_STR(CV_VERSION_MAJOR)""CVAUX_STR(CV_VERSION_MINOR)""CVAUX_STR(CV_VERSION_REVISION)
#pragma comment(lib, "opencv_world" OPENCV_VERSION ".lib")
#else
#define OPENCV_VERSION CVAUX_STR(CV_VERSION_EPOCH)""CVAUX_STR(CV_VERSION_MAJOR)""CVAUX_STR(CV_VERSION_MINOR)
#pragma comment(lib, "opencv_core" OPENCV_VERSION ".lib")
#pragma comment(lib, "opencv_imgproc" OPENCV_VERSION ".lib")
#pragma comment(lib, "opencv_highgui" OPENCV_VERSION ".lib")
#endif

#endif

void draw_detections_cv_v3(IplImage* show_img, detection *dets, int num, float thresh, char **names, int classes, int ext_output);


int get_stream_fps(CvCapture *cap, int cpp_video_capture)
{
	int fps = 25;
	fps = cvGetCaptureProperty(cap, CV_CAP_PROP_FPS);
	return fps;
}

void mean_arrays(float **a, int n, int els, float *avg)
{
	int i;
	int j;
	memset(avg, 0, els*sizeof(float));
	for (j = 0; j < n; ++j) {
		for (i = 0; i < els; ++i) {
			avg[i] += a[j][i];
		}
	}
	for (i = 0; i < els; ++i) {
		avg[i] /= n;
	}
}


// get prediction boxes: yolov2_forward_network.c
void get_region_boxes_cpu(layer *l, int w, int h, float thresh, float **probs, box *boxes, int only_objectness, int *map);

typedef struct detection_with_class {
    detection det;
    // The most probable class id: the best class index in this->prob.
    // Is filled temporary when processing results, otherwise not initialized
    int best_class;
} detection_with_class;

// Creates array of detections with prob > thresh and fills best_class for them
detection_with_class* get_actual_detections(detection *dets, int dets_num, float thresh, int* selected_detections_num)
{
    int selected_num = 0;
    detection_with_class* result_arr = calloc(dets_num, sizeof(detection_with_class));
    int i;
    for (i = 0; i < dets_num; ++i) {
        int best_class = -1;
        float best_class_prob = thresh;
        int j;
        for (j = 0; j < dets[i].classes; ++j) {
            if (dets[i].prob[j] > best_class_prob) {
                best_class = j;
                best_class_prob = dets[i].prob[j];
            }
        }
        if (best_class >= 0) {
            result_arr[selected_num].det = dets[i];
            result_arr[selected_num].best_class = best_class;
            ++selected_num;
        }
    }
    if (selected_detections_num)
        *selected_detections_num = selected_num;
    return result_arr;
}

// compare to sort detection** by bbox.x
int compare_by_lefts(const void *a_ptr, const void *b_ptr) {
    const detection_with_class* a = (detection_with_class*)a_ptr;
    const detection_with_class* b = (detection_with_class*)b_ptr;
    const float delta = (a->det.bbox.x - a->det.bbox.w / 2) - (b->det.bbox.x - b->det.bbox.w / 2);
    return delta < 0 ? -1 : delta > 0 ? 1 : 0;
}

// compare to sort detection** by best_class probability
int compare_by_probs(const void *a_ptr, const void *b_ptr) {
    const detection_with_class* a = (detection_with_class*)a_ptr;
    const detection_with_class* b = (detection_with_class*)b_ptr;
    float delta = a->det.prob[a->best_class] - b->det.prob[b->best_class];
    return delta < 0 ? -1 : delta > 0 ? 1 : 0;
}

int compare_by_box_area(const void *a_ptr, const void *b_ptr) {
	const detection_with_class* a = (detection_with_class*)a_ptr;
	const detection_with_class* b = (detection_with_class*)b_ptr;
	const float delta = (a->det.bbox.w*a->det.bbox.h) - (b->det.bbox.w*b->det.bbox.h);
	return delta < 0 ? -1 : delta > 0 ? 1 : 0;
}

void draw_detections_v3(image im, detection *dets, int num, float thresh, char **names, image **alphabet, int classes, int ext_output)
{
    int selected_detections_num;
    detection_with_class* selected_detections = get_actual_detections(dets, num, thresh, &selected_detections_num);

    // text output
    qsort(selected_detections, selected_detections_num, sizeof(*selected_detections), compare_by_lefts);
    int i;
    for (i = 0; i < selected_detections_num; ++i) {
        const int best_class = selected_detections[i].best_class;
        printf("%s: %.0f%%", names[best_class], selected_detections[i].det.prob[best_class] * 100);
        if (ext_output)
            printf("\t(left_x: %4.0f   top_y: %4.0f   width: %4.0f   height: %4.0f)\n",
                round((selected_detections[i].det.bbox.x - selected_detections[i].det.bbox.w / 2)*im.w),
                round((selected_detections[i].det.bbox.y - selected_detections[i].det.bbox.h / 2)*im.h),
                round(selected_detections[i].det.bbox.w*im.w), round(selected_detections[i].det.bbox.h*im.h));
        else
            printf("\n");
        int j;
        for (j = 0; j < classes; ++j) {
            if (selected_detections[i].det.prob[j] > thresh && j != best_class) {
                printf("%s: %.0f%%\n", names[j], selected_detections[i].det.prob[j] * 100);
            }
        }
    }

    // image output
    qsort(selected_detections, selected_detections_num, sizeof(*selected_detections), compare_by_probs);
    for (i = 0; i < selected_detections_num; ++i) {
        int width = im.h * .006;
        if (width < 1)
            width = 1;

        /*
        if(0){
        width = pow(prob, 1./2.)*10+1;
        alphabet = 0;
        }
        */

        //printf("%d %s: %.0f%%\n", i, names[selected_detections[i].best_class], prob*100);
        int offset = selected_detections[i].best_class * 123457 % classes;
        float red = get_color(2, offset, classes);
        float green = get_color(1, offset, classes);
        float blue = get_color(0, offset, classes);
        float rgb[3];

        //width = prob*20+2;

        rgb[0] = red;
        rgb[1] = green;
        rgb[2] = blue;
        box b = selected_detections[i].det.bbox;
        //printf("%f %f %f %f\n", b.x, b.y, b.w, b.h);

        int left = (b.x - b.w / 2.)*im.w;
        int right = (b.x + b.w / 2.)*im.w;
        int top = (b.y - b.h / 2.)*im.h;
        int bot = (b.y + b.h / 2.)*im.h;

        if (left < 0) left = 0;
        if (right > im.w - 1) right = im.w - 1;
        if (top < 0) top = 0;
        if (bot > im.h - 1) bot = im.h - 1;

        draw_box_width(im, left, top, right, bot, width, red, green, blue);
    }
    free(selected_detections);
}



// --------------- Detect on the Image ---------------


// Detect on Image: this function uses other functions not from this file
void test_detector_cpu(char **names, char *cfgfile, char *weightfile, char *filename, float thresh, int quantized, int dont_show)
{
    //image **alphabet = load_alphabet();            // image.c
    image **alphabet = NULL;
    network net = parse_network_cfg(cfgfile, 1, quantized);    // parser.c
    if (weightfile) {
        load_weights_upto_cpu(&net, weightfile, net.n);    // parser.c
    }
    //set_batch_network(&net, 1);                    // network.c
    srand(2222222);
    yolov2_fuse_conv_batchnorm(&net);
    calculate_binary_weights(&net);
    if (quantized) {
        printf("\n\n Quantinization! \n\n");
        quantinization_and_get_multipliers(&net); //Quantization
    }
    clock_t time;
    char buff[256];
    char *input = buff;
    int j;
    float nms = .4;
    while (1) {
        if (filename) {
            strncpy(input, filename, 256);
        }
        else {
            printf("Enter Image Path: ");
            fflush(stdout);
            input = fgets(input, 256, stdin);
            if (!input) return;
            strtok(input, "\n");
        }
        image im = load_image(input, 0, 0, 3);            // image.c
        image sized = resize_image(im, net.w, net.h);    // image.c
        layer l = net.layers[net.n - 1];

        box *boxes = calloc(l.w*l.h*l.n, sizeof(box));
        float **probs = calloc(l.w*l.h*l.n, sizeof(float *));
        for (j = 0; j < l.w*l.h*l.n; ++j) probs[j] = calloc(l.classes, sizeof(float *));

        float *X = sized.data;
        time = clock();
        //network_predict(net, X);
#ifdef GPU
        if (quantized) {
            network_predict_gpu_cudnn
				uantized(net, X);    // quantized works only with Yolo v2
                                                            //nms = 0.2;
        }
        else {
            network_predict_gpu_cudnn(net, X);
        }
#else
#ifdef OPENCL
        network_predict_opencl(net, X);
#else
        if (quantized) {
            network_predict_quantized(&net, X);    // quantized works only with Yolo v2
            nms = 0.2;
        }
        else {
            network_predict_cpu(&net, X);
        }
#endif
#endif
        printf("%s: Predicted in %f seconds.\n", input, (float)(clock() - time) / CLOCKS_PER_SEC); //sec(clock() - time));
        //get_region_boxes_cpu(l, 1, 1, thresh, probs, boxes, 0, 0);            // get_region_boxes(): region_layer.c

        //  nms (non maximum suppression) - if (IoU(box[i], box[j]) > nms) then remove one of two boxes with lower probability
        //if (nms) do_nms_sort(boxes, probs, l.w*l.h*l.n, l.classes, nms);    // box.c
        //draw_detections_cpu(im, l.w*l.h*l.n, thresh, boxes, probs, names, alphabet, l.classes);    // draw_detections(): image.c
        float hier_thresh = 0.5;
        int ext_output = 1, letterbox = 0, nboxes = 0;
        detection *dets = get_network_boxes(&net, im.w, im.h, thresh, hier_thresh, 0, 1, &nboxes, letterbox);
        if (nms) do_nms_sort(dets, nboxes, l.classes, nms);
        draw_detections_v3(im, dets, nboxes, thresh, names, alphabet, l.classes, ext_output);

        save_image_png(im, "predictions");    // image.c
        if (!dont_show) {
            show_image(im, "predictions");    // image.c
        }

        free_image(im);                    // image.c
        free_image(sized);                // image.c
        free(boxes);
        free_ptrs((void **)probs, l.w*l.h*l.n);    // utils.c
#ifdef OPENCV
        cvWaitKey(0);
        cvDestroyAllWindows();
#endif
        if (filename) break;
    }
}


// --------------- Detect on the Video ---------------

#ifdef OPENCV
static char **demo_names;
static int demo_classes;
static int demo_quantized;

static float **probs;
static box *boxes;
static network net;
static image in;
static image in_s;
static image det;
static image det_s;
static image disp = { 0 };
static CvCapture * cap;
static float fps = 0;
static float demo_thresh = 0;

#define FRAMES 3
static int cpp_video_capture = 0;
static float *predictions[FRAMES];
static image images[FRAMES];
static float *avg;
static int flag_exit;
static int demo_index = 0;
static IplImage* ipl_images[FRAMES];
static long long int frame_id = 0;
static int demo_ext_output = 0;

IplImage* in_img;
IplImage* det_img;
IplImage* show_img;

// draw bounded boxes of found objects on the image, from: image.c
void draw_detections_cv_v3(IplImage* show_img, detection *dets, int num, float thresh, char **names, int classes, int ext_output)
{
	/*
    int i, j;
    if (!show_img) return;
    static int frame_id = 0;
    frame_id++;

    for (i = 0; i < num; ++i) {
        char labelstr[4096] = { 0 };
        int class_id = -1;
        for (j = 0; j < classes; ++j) {
            if (dets[i].prob[j] > thresh) {
                if (class_id < 0) {
                    strcat(labelstr, names[j]);
                    class_id = j;
                }
                else {
                    strcat(labelstr, ", ");
                    strcat(labelstr, names[j]);
                }
                printf("%s: %.0f%% ", names[j], dets[i].prob[j] * 100);
            }
        }
        if (class_id >= 0) {
            int width = show_img->height * .006;

            //printf("%d %s: %.0f%%\n", i, names[class_id], prob*100);
            int offset = class_id * 123457 % classes;
            float red = get_color(2, offset, classes);
            float green = get_color(1, offset, classes);
            float blue = get_color(0, offset, classes);
            float rgb[3];

            //width = prob*20+2;

            rgb[0] = red;
            rgb[1] = green;
            rgb[2] = blue;
            box b = dets[i].bbox;
            

			b.w = (b.w < 1) ? b.w : 1;
			b.h = (b.h < 1) ? b.h : 1;
			b.x = (b.x < 1) ? b.x : 1;
			b.y = (b.y < 1) ? b.y : 1;

			//printf("%f %f %f %f\n", b.x, b.y, b.w, b.h);

            int left = (b.x - b.w / 2.)*show_img->width;
            int right = (b.x + b.w / 2.)*show_img->width;
            int top = (b.y - b.h / 2.)*show_img->height;
            int bot = (b.y + b.h / 2.)*show_img->height;

            if (left < 0) left = 0;
            if (right > show_img->width - 1) right = show_img->width - 1;
            if (top < 0) top = 0;
            if (bot > show_img->height - 1) bot = show_img->height - 1;

            float const font_size = show_img->height / 1000.F;
            CvPoint pt1, pt2, pt_text, pt_text_bg1, pt_text_bg2;
            pt1.x = left;
            pt1.y = top;
            pt2.x = right;
            pt2.y = bot;
            pt_text.x = left;
            pt_text.y = top - 12;
            pt_text_bg1.x = left;
            pt_text_bg1.y = top - (10 + 25 * font_size);
            pt_text_bg2.x = right;
            pt_text_bg2.y = top;
            CvScalar color;
            color.val[0] = red * 256;
            color.val[1] = green * 256;
            color.val[2] = blue * 256;

            cvRectangle(show_img, pt1, pt2, color, width, 8, 0);
            if (ext_output)
                printf("\t(left_x: %4.0f   top_y: %4.0f   width: %4.0f   height: %4.0f)\n",
                (float)left, (float)top, b.w*show_img->width, b.h*show_img->height);
            else
                printf("\n");
            cvRectangle(show_img, pt_text_bg1, pt_text_bg2, color, width, 8, 0);
            cvRectangle(show_img, pt_text_bg1, pt_text_bg2, color, CV_FILLED, 8, 0);    // filled
            CvScalar black_color;
            black_color.val[0] = 0;
            CvFont font;
            cvInitFont(&font, CV_FONT_HERSHEY_SIMPLEX, font_size, font_size, 0, font_size * 3, 8);
            cvPutText(show_img, labelstr, pt_text, &font, black_color);
        }
    }
	*/
	int i, j;
	char labelstr[4096] = { 0 };
	if (!show_img) return;
	static int frame_id = 0;
	frame_id++;
	int selected_detections_num;

	int *class_det_num = (int*)malloc(sizeof(int)*classes);

	for (int i = 0;i < classes;i++)
	{
		class_det_num[i] = 0;
	}



	detection_with_class* selected_detections = get_actual_detections(dets, num, thresh, &selected_detections_num);

	qsort(selected_detections, selected_detections_num, sizeof(*selected_detections), compare_by_box_area);

	for (i = 0; i < selected_detections_num; ++i) {
		int width = show_img->height * .006;
		if (width < 1)
			width = 1;

		class_det_num[selected_detections[i].best_class] += 1;

		if (class_det_num[selected_detections[i].best_class]>1)
		{
			continue;
		}


		int offset = selected_detections[i].best_class * 123457 % classes;
		//float red = get_color(2, offset, classes);
		//float green = get_color(1, offset, classes);
		//float blue = get_color(0, offset, classes);
		//float rgb[3];

		//width = prob*20+2;

		//rgb[0] = red;
		//rgb[1] = green;
		//rgb[2] = blue;
		box b = selected_detections[i].det.bbox;
		b.w = (b.w < 1) ? b.w : 1;
		b.h = (b.h < 1) ? b.h : 1;
		b.x = (b.x < 1) ? b.x : 1;
		b.y = (b.y < 1) ? b.y : 1;
		//printf("%f %f %f %f\n", b.x, b.y, b.w, b.h);

		int left = (b.x - b.w / 2.)*show_img->width;
		int right = (b.x + b.w / 2.)*show_img->width;
		int top = (b.y - b.h / 2.)*show_img->height;
		int bot = (b.y + b.h / 2.)*show_img->height;

		if (left < 0) left = 0;
		if (right > show_img->width - 1) right = show_img->width - 1;
		if (top < 0) top = 0;
		if (bot > show_img->height - 1) bot = show_img->height - 1;

		//int b_x_center = (left + right) / 2;
		//int b_y_center = (top + bot) / 2;
		//int b_width = right - left;
		//int b_height = bot - top;
		//sprintf(labelstr, "%d x %d - w: %d, h: %d", b_x_center, b_y_center, b_width, b_height);

		float const font_size = show_img->height / 1000.F;
		CvPoint pt1, pt2, pt_text, pt_text_bg1, pt_text_bg2;
		pt1.x = left;
		pt1.y = top;
		pt2.x = right;
		pt2.y = bot;
		pt_text.x = left;
		pt_text.y = top - 12;
		pt_text_bg1.x = left;
		pt_text_bg1.y = top - (10 + 25 * font_size);
		pt_text_bg2.x = right;
		pt_text_bg2.y = top;
		CvScalar color;
		CvScalar black_color;
		if (strstr(names[selected_detections[i].best_class], "CLOSED") != NULL)
		{
			color.val[0] = 0;
			color.val[1] = 0;
			color.val[2] = 0;
			black_color.val[0] = 255;
		}
		else {
			color.val[0] = 255;
			color.val[1] = 255;
			color.val[2] = 255;
			black_color.val[0] = 0;
		}
		// you should create directory: result_img
		//static int copied_frame_id = -1;
		//static IplImage* copy_img = NULL;
		//if (copied_frame_id != frame_id) {
		//    copied_frame_id = frame_id;
		//    if(copy_img == NULL) copy_img = cvCreateImage(cvSize(show_img->width, show_img->height), show_img->depth, show_img->nChannels);
		//    cvCopy(show_img, copy_img, 0);
		//}
		//static int img_id = 0;
		//img_id++;
		//char image_name[1024];
		//sprintf(image_name, "result_img/img_%d_%d_%d.jpg", frame_id, img_id, class_id);
		//CvRect rect = cvRect(pt1.x, pt1.y, pt2.x - pt1.x, pt2.y - pt1.y);
		//cvSetImageROI(copy_img, rect);
		//cvSaveImage(image_name, copy_img, 0);
		//cvResetImageROI(copy_img);

		cvRectangle(show_img, pt1, pt2, color, width, 8, 0);
		if (ext_output)
			printf("\t(left_x: %4.0f   top_y: %4.0f   width: %4.0f   height: %4.0f)\n",
				(float)left, (float)top, b.w*show_img->width, b.h*show_img->height);
		else
			printf("\n");

		cvRectangle(show_img, pt_text_bg1, pt_text_bg2, color, width, 8, 0);
		cvRectangle(show_img, pt_text_bg1, pt_text_bg2, color, CV_FILLED, 8, 0);    // filled
		CvFont font;
		cvInitFont(&font, CV_FONT_HERSHEY_SIMPLEX, font_size, font_size, 0, font_size * 3, 8);
		sprintf(labelstr, "%s : %3.2f %", names[selected_detections[i].best_class], *selected_detections[i].det.prob);
		printf("%s\n", labelstr);
		cvPutText(show_img, labelstr, pt_text, &font, black_color);
	}
	if (ext_output) {
		fflush(stdout);
	}
}



image get_image_from_stream_resize_cpu(CvCapture *cap, int w, int h, IplImage** in_img)
{
    IplImage* src = cvQueryFrame(cap);
    if (!src) return make_empty_image(0, 0, 0);
    IplImage* new_img = cvCreateImage(cvSize(w, h), IPL_DEPTH_8U, 3);
    *in_img = cvCreateImage(cvSize(src->width, src->height), IPL_DEPTH_8U, 3);
    cvResize(src, *in_img, CV_INTER_LINEAR);
    cvResize(src, new_img, CV_INTER_LINEAR);
    image im = ipl_to_image(new_img);
    cvReleaseImage(&new_img);
    rgbgr_image(im);
    return im;
}

image get_image_from_stream_resize(CvCapture *cap, int w, int h, int c, IplImage** in_img)
{
	c = c ? c : 3;
	IplImage* src;
	IplImage* gray;
	src = cvQueryFrame(cap);
	gray = cvCreateImage(cvSize(src->width, src->height), IPL_DEPTH_8U, c);

	if (c == 1 && src->nChannels == 3)
	{

		cvCvtColor(src, gray, CV_BGR2GRAY);
	}


	IplImage* new_img = cvCreateImage(cvSize(w, h), IPL_DEPTH_8U, c);
	*in_img = cvCreateImage(cvSize(src->width, src->height), IPL_DEPTH_8U, c);
	if (c == 1)
	{
		cvResize(gray, *in_img, CV_INTER_LINEAR);
		cvResize(gray, new_img, CV_INTER_LINEAR);
	}
	else
	{
		cvResize(src, *in_img, CV_INTER_LINEAR);
		cvResize(src, new_img, CV_INTER_LINEAR);
	}

	image im = ipl_to_image(new_img);
	cvReleaseImage(&new_img);
	if (cpp_video_capture) cvReleaseImage(&src);
	if (c>1)
		rgbgr_image(im);
	return im;
}

static void *fetch_in_thread(void *ptr)
{
	/*
    in = get_image_from_stream_resize_cpu(cap, net.w, net.h, &in_img);    // image.c
    if (!in.data) {
        error("Stream closed.");
    }
    in_s = make_image(in.w, in.h, in.c);    // image.c
    memcpy(in_s.data, in.data, in.h*in.w*in.c * sizeof(float));

    return 0;
	*/
	
	//in_s = get_image_from_stream_resize_cpu(cap, net.w, net.h, &in_img);
	in_s = get_image_from_stream_resize(cap, net.w, net.h, net.c, &in_img);
	if (!in_s.data) {
		//error("Stream closed.");
		printf("Stream closed.\n");
		flag_exit = 1;
		return EXIT_FAILURE;
	}
	//in_s = resize_image(in, net.w, net.h);

	return 0;
}

static void *detect_in_thread(void *ptr)
{
    float nms = .4;
    layer l = net.layers[net.n - 1];
    float *X = det_s.data;
	float *prediction = (float*)calloc(1, sizeof(float));
    //float *prediction = network_predict(net, X);
#ifdef GPU
    if (demo_quantized) {
        network_predict_gpu_cudnn_quantized(net, X);    // quantized works only with Yolo v2
                                                        //nms = 0.2;
    }
    else {
        network_predict_gpu_cudnn(net, X);
    }
#else
#ifdef OPENCL
    network_predict_opencl(net, X);
#else
    if (demo_quantized) {
		prediction=network_predict_quantized(&net, X);    // quantized works only with Yolo v2
        //nms = 0.2;
    }
    else {
        network_predict_cpu(&net, X);
    }
#endif
#endif

	memcpy(predictions[demo_index], prediction, l.outputs*sizeof(float));
	mean_arrays(predictions, FRAMES, l.outputs, avg);
	l.output = avg;

    free_image(det_s);
    //get_region_boxes_cpu(l, 1, 1, demo_thresh, probs, boxes, 0, 0);        // get_region_boxes(): region_layer.c
    //if (nms) do_nms_sort(boxes, probs, l.w*l.h*l.n, l.classes, nms);    // box.c
    
	float hier_thresh = 0.5;
    int ext_output = 1, letterbox = 0, nboxes = 0;

	detection *dets = NULL;
    if (letterbox)
        dets = get_network_boxes(&net, in_img->width, in_img->height, demo_thresh, demo_thresh, 0, 1, &nboxes, 1); // letter box
    else
        dets = get_network_boxes(&net, det_s.w, det_s.h, demo_thresh, demo_thresh, 0, 1, &nboxes, 0); // resized
                                                                                                      //if (nms) do_nms_obj(dets, nboxes, l.classes, nms);    // bad results
    if (nms) do_nms_sort(dets, nboxes, l.classes, nms);
    //draw_detections_cv_v3(det_img, dets, nboxes, demo_thresh, demo_names, demo_classes, ext_output);
    //free_detections(dets, nboxes);

    printf("\033[2J");
    printf("\033[1;1H");
    printf("\nFPS:%.1f\n", fps);
    printf("Objects:\n\n");

	ipl_images[demo_index] = det_img;
	det_img = ipl_images[(demo_index + FRAMES / 2 + 1) % FRAMES];
	demo_index = (demo_index + 1) % FRAMES;

	++frame_id;

	draw_detections_cv_v3(det_img, dets, nboxes, demo_thresh, demo_names, demo_classes, demo_ext_output);
	free_detections(dets, nboxes);

    return 0;
}

static double get_wall_time()
{
    struct timeval time;
    if (gettimeofday(&time, NULL)) {
        return 0;
    }
    return (double)time.tv_sec + (double)time.tv_usec * .000001;
}


// Detect on Video: this function uses other functions not from this file
void demo(char *cfgfile, char *weightfile, float thresh, int cam_index, const char *filename, char **names, int classes,
    int frame_skip, char *prefix, int quantized, char *out_filename, int dont_show)
{
    int delay = frame_skip;
	//image **alphabet = load_alphabet();
    demo_names = names;
    demo_classes = classes;
    demo_thresh = thresh;
    printf("Demo\n");
    net = parse_network_cfg(cfgfile, 1, quantized);
    if (weightfile) {
        //load_weights(&net, weightfile);            // parser.c
        load_weights_upto_cpu(&net, weightfile, net.n);
    }
    //set_batch_network(&net, 1);
    yolov2_fuse_conv_batchnorm(&net);
    calculate_binary_weights(&net);
    if (quantized) {
        printf("\n\n Quantinization! \n\n");
        demo_quantized = 1;
        quantinization_and_get_multipliers(&net);
    }
    srand(2222222);

	if (filename) {
		printf("video file: %s\n", filename);
		//cpp_video_capture = 1;
		cap = get_capture_video_stream(filename);
		//cap = cvCaptureFromFile(filename);
	}
	else {
		printf("Webcam index: %d\n", cam_index);
		//cpp_video_capture = 1;
		//cap = cvCaptureFromCAM(cam_index);
		//cap = cvCreateCameraCapture(cam_index);
		cap = get_capture_webcam(cam_index);
	}

	if (!cap) {
#ifdef WIN32
		printf("Check that you have copied file opencv_ffmpeg340_64.dll to the same directory where is darknet.exe \n");
#endif
		error("Couldn't connect to webcam.\n");
	}

    layer l = net.layers[net.n - 1];
    int j;

	avg = (float *)calloc(l.outputs, sizeof(float));
	for (j = 0; j < FRAMES; ++j) predictions[j] = (float *)calloc(l.outputs, sizeof(float));
	for (j = 0; j < FRAMES; ++j) images[j] = make_image(1, 1, 3);

    boxes = (box *)calloc(l.w*l.h*l.n, sizeof(box));
    probs = (float **)calloc(l.w*l.h*l.n, sizeof(float*));
    for (j = 0; j < l.w*l.h*l.n; ++j) probs[j] = (float *)calloc(l.classes, sizeof(float*));

	if (l.classes != demo_classes) {
		printf("Parameters don't match: in cfg-file classes=%d, in data-file classes=%d \n", l.classes, demo_classes);
		getchar();
		exit(0);
	}


	flag_exit = 0;


    pthread_t fetch_thread;
    pthread_t detect_thread;

    fetch_in_thread(0);
    det_img = in_img;
    det = in;
    det_s = in_s;

    fetch_in_thread(0);
    detect_in_thread(0);
    disp = det;
    show_img = det_img;
    det_img = in_img;
    det = in;
    det_s = in_s;

    int count = 0;
    if (!prefix && !dont_show) {
        cvNamedWindow("Demo", CV_WINDOW_NORMAL);
        cvMoveWindow("Demo", 0, 0);
        cvResizeWindow("Demo", 1352, 1013);
    }

    CvVideoWriter* output_video_writer = NULL;    // cv::VideoWriter output_video;
    if (out_filename && !flag_exit)
    {
        CvSize size;
        size.width = det_img->width, size.height = det_img->height;
        int src_fps = 25;
        //src_fps = cvGetCaptureProperty(cap, CV_CAP_PROP_FPS);
		src_fps = get_stream_fps(cap, cpp_video_capture);
		output_video_writer = cvCreateVideoWriter(out_filename, CV_FOURCC('D', 'I', 'V', 'X'), src_fps, size, 1);
    }

    double before = get_wall_time();

    while (1) {
        ++count;
        if (pthread_create(&fetch_thread, 0, fetch_in_thread, 0)) error("Thread creation failed");
        if (pthread_create(&detect_thread, 0, detect_in_thread, 0)) error("Thread creation failed");

        if (!prefix) {
            if (!dont_show) {
                //show_image(disp, "Demo");
                show_image_cv_ipl(show_img, "Demo");
                int c = cvWaitKey(1);
				if (c == 10) {
					if (frame_skip == 0) frame_skip = 60;
					else if (frame_skip == 4) frame_skip = 0;
					else if (frame_skip == 60) frame_skip = 4;
					else frame_skip = 0;
				}
				else if (c == 27 || c == 1048603) // ESC - exit (OpenCV 2.x / 3.x)
				{
					flag_exit = 1;
				}
            }
        }
        else {
            char buff[256];
            sprintf(buff, "%s_%08d", prefix, count);
			if(show_img) save_image_png(disp, buff);
        }

        // save video file
        if (output_video_writer && show_img) {
            cvWriteFrame(output_video_writer, show_img);
            //printf("\n cvWriteFrame \n");
        }

        cvReleaseImage(&show_img);

        pthread_join(fetch_thread, 0);
        pthread_join(detect_thread, 0);

		if (flag_exit == 1) break;

        if (delay == 0) {
            free_image(disp);
            disp = det;
            show_img = det_img;
        }
        det_img = in_img;
        det = in;
        det_s = in_s;

        --delay;
        if (delay < 0) {
            delay = frame_skip;

            double after = get_wall_time();
            float curr = 1. / (after - before);
            fps = curr;
            before = after;
        }
    }
	printf("input video stream closed. \n");
	if (output_video_writer) {
		cvReleaseVideoWriter(&output_video_writer);
		printf("output_video_writer closed. \n");
	}

	// free memory
	cvReleaseImage(&show_img);
	cvReleaseImage(&in_img);
	free_image(in_s);

	free(avg);
	for (j = 0; j < FRAMES; ++j) free(predictions[j]);
	for (j = 0; j < FRAMES; ++j) free_image(images[j]);

	for (j = 0; j < l.w*l.h*l.n; ++j) free(probs[j]);
	free(boxes);
	free(probs);

	free_ptrs(names, net.layers[net.n - 1].classes);


	free_network(net);

}
#else
void demo(char *cfgfile, char *weightfile, float thresh, int cam_index, const char *filename, char **names, int classes,
    int frame_skip, char *prefix, int quantized, char *out_filename, int dont_show)
{
    fprintf(stderr, "Demo needs OpenCV for webcam images.\n");
}
#endif


// get command line parameters and load objects names
void run_detector(int argc, char **argv)
{
    int dont_show = find_arg(argc, argv, "-dont_show");
    char *prefix = find_char_arg(argc, argv, "-prefix", 0);
    float thresh = find_float_arg(argc, argv, "-thresh", .25);
    float iou_thresh = find_float_arg(argc, argv, "-iou_thresh", .5);    // 0.5 for mAP
    char *out_filename = find_char_arg(argc, argv, "-out_filename", 0);
    int cam_index = find_int_arg(argc, argv, "-c", 0);
    int quantized = find_arg(argc, argv, "-quantized");
    int input_calibration = find_int_arg(argc, argv, "-input_calibration", 0);
    int frame_skip = find_int_arg(argc, argv, "-s", 0);
    if (argc < 4) {
        fprintf(stderr, "usage: %s %s [demo/test/] [cfg] [weights (optional)]\n", argv[0], argv[1]);
        return;
    }

    int clear = 0;                // find_arg(argc, argv, "-clear");

    char *datacfg = argv[3];
	char *cfg = argv[4];
	char *weights = (argc > 5) ? argv[5] : 0;
	if (weights)
		if (strlen(weights) > 0)
			if (weights[strlen(weights) - 1] == 0x0d) weights[strlen(weights) - 1] = 0;
	char *filename = (argc > 6) ? argv[6] : 0;
    

    if (0 == strcmp(argv[2], "test")) test_detector_cpu(datacfg, cfg, weights, filename, thresh, quantized, dont_show);
    //else if (0 == strcmp(argv[2], "train")) train_detector(datacfg, cfg, weights, gpus, ngpus, clear);
    //else if (0 == strcmp(argv[2], "valid")) validate_detector(datacfg, cfg, weights);
    //else if (0 == strcmp(argv[2], "recall")) validate_detector_recall(datacfg, cfg, weights);
    else if (0 == strcmp(argv[2], "map")) validate_detector_map(datacfg, cfg, weights, thresh, quantized, iou_thresh);
    else if (0 == strcmp(argv[2], "calibrate")) validate_calibrate_valid(datacfg, cfg, weights, input_calibration);
    else if (0 == strcmp(argv[2], "demo")) {
		list *options = read_data_cfg(datacfg);
		int classes = option_find_int(options, "classes", 20);
		char *name_list = option_find_str(options, "names", "data/names.list");
		char **names = get_labels(name_list);
		if (filename)
			if (strlen(filename) > 0)
				if (filename[strlen(filename) - 1] == 0x0d) filename[strlen(filename) - 1] = 0;
        demo(cfg, weights, thresh, cam_index, filename, names, classes, frame_skip, prefix, quantized, out_filename, dont_show);
		
		free_list_contents_kvp(options);
		free_list(options);
	}
	else printf(" There isn't such command: %s", argv[2]);
	
}


int main(int argc, char **argv)
{
#ifdef _DEBUG
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif
    int i;
    for (i = 0; i < argc; ++i) {
        if (!argv[i]) continue;
        strip(argv[i]);
    }

    if (argc < 2) {
        fprintf(stderr, "usage: %s <function>\n", argv[0]);
        return 0;
    }
    gpu_index = find_int_arg(argc, argv, "-i", 0);  //  gpu_index = 0;

#ifndef GPU
    gpu_index = -1;
#else
    if (gpu_index >= 0) {
        cuda_set_device(gpu_index);
    }
#endif
#ifdef OPENCL
    ocl_initialize();
#endif
    run_detector(argc, argv);
    return 0;
}
