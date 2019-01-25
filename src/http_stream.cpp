#include "http_stream.h"

#ifdef OPENCV
//
// a single-threaded, multi client(using select), debug webserver - streaming out mjpg.
//  on win, _WIN32 has to be defined, must link against ws2_32.lib (socks on linux are for free)
//

//
// socket related abstractions:
//
#ifdef _WIN32
#pragma comment(lib, "ws2_32.lib")
#include <winsock.h>
#include <windows.h>
#include <time.h>
#define PORT        unsigned long
#define ADDRPOINTER   int*
struct _INIT_W32DATA
{
	WSADATA w;
	_INIT_W32DATA() { WSAStartup(MAKEWORD(2, 1), &w); }
} _init_once;
#else       /* ! win32 */
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#define PORT        unsigned short
#define SOCKET    int
#define HOSTENT  struct hostent
#define SOCKADDR    struct sockaddr
#define SOCKADDR_IN  struct sockaddr_in
#define ADDRPOINTER  unsigned int*
#define INVALID_SOCKET -1
#define SOCKET_ERROR   -1
#endif /* _WIN32 */

#include <cstdio>
#include <vector>
#include <iostream>
#include <algorithm>
using std::cerr;
using std::endl;

#include "opencv2/opencv.hpp"
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/highgui/highgui_c.h"
#include "opencv2/imgproc/imgproc_c.h"
#ifndef CV_VERSION_EPOCH
#include "opencv2/videoio/videoio.hpp"
#define OPENCV_VERSION CVAUX_STR(CV_VERSION_MAJOR)""CVAUX_STR(CV_VERSION_MINOR)""CVAUX_STR(CV_VERSION_REVISION)
#pragma comment(lib, "opencv_world" OPENCV_VERSION ".lib")
#endif
using namespace cv;
#include "additionally.h"

CvCapture* get_capture_video_stream(char *path) {
	CvCapture* cap = NULL;
	try {
		cap = (CvCapture*)new cv::VideoCapture(path);
	}
	catch (...) {
		std::cout << " Error: video-stream " << path << " can't be opened! \n";
	}
	return cap;
}
// ----------------------------------------

CvCapture* get_capture_webcam(int index) {
	CvCapture* cap = NULL;
	try {
		cap = (CvCapture*)new cv::VideoCapture(index);
		//((cv::VideoCapture*)cap)->set(CV_CAP_PROP_FRAME_WIDTH, 1280);
		//((cv::VideoCapture*)cap)->set(CV_CAP_PROP_FRAME_HEIGHT, 960);
	}
	catch (...) {
		std::cout << " Error: Web-camera " << index << " can't be opened! \n";
	}
	return cap;
}
// ----------------------------------------

IplImage* get_webcam_frame(CvCapture *cap) {
	IplImage* src = NULL;
	try {
		cv::VideoCapture &cpp_cap = *(cv::VideoCapture *)cap;
		cv::Mat frame;
		if (cpp_cap.isOpened())
		{
			cpp_cap >> frame;
			IplImage tmp = frame;
			src = cvCloneImage(&tmp);
		}
		else {
			std::cout << " Video-stream stoped! \n";
		}
	}
	catch (...) {
		std::cout << " Video-stream stoped! \n";
	}
	return src;
}

int get_stream_fps_cpp(CvCapture *cap) {
	int fps = 25;
	try {
		cv::VideoCapture &cpp_cap = *(cv::VideoCapture *)cap;
#ifndef CV_VERSION_EPOCH    // OpenCV 3.x
		fps = cpp_cap.get(CAP_PROP_FPS);
#else                        // OpenCV 2.x
		fps = cpp_cap.get(CV_CAP_PROP_FPS);
#endif
	}
	catch (...) {
		std::cout << " Can't get FPS of source videofile. For output video FPS = 25 by default. \n";
	}
	return fps;
}

#endif    // OPENCV

#if __cplusplus >= 201103L || _MSC_VER >= 1900  // C++11
#else // C++11
#endif // C++11