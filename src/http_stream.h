#pragma once
#ifndef HTTP_STREAM_H
#define HTTP_STREAM_H
#include "additionally.h"
#ifdef OPENCV
#include "opencv2/highgui/highgui_c.h"
#include "opencv2/imgproc/imgproc_c.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#ifdef OPENCV

	CvCapture* get_capture_webcam(int index);
	CvCapture* get_capture_video_stream(char *path);
	IplImage* get_webcam_frame(CvCapture *cap);
	int get_stream_fps_cpp(CvCapture *cap);

#endif  // OPENCV



#ifdef __cplusplus
}
#endif

#endif // HTTP_STREAM_H