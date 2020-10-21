#include "opencv2/core/core.hpp"
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/imgproc/imgproc.hpp"
#include <iostream>
#include <stdio.h>
#include <ctime>

#include "IPM.h"

using namespace cv;
using namespace std;

int main( int _argc, char** _argv )
{
	// Images
	Mat inputImg, inputImgGray;
	Mat outputImg;
	
	if( _argc != 2 )
	{
		cout << "Usage: ipm <imagefile>" << endl;
		return 1;
	}
	inputImg = imread(_argv[1], IMREAD_COLOR);
	if(inputImg.empty())
    {
        cout << "Could not read the image: " << endl;
        return 1;
    }

	// // Video
	// string videoFileName = _argv[1];	
	// cv::VideoCapture video;
	// if( !video.open(videoFileName) )
	// 	return 1;

	// Show image information
	int width = 0, height = 0;
	width = static_cast<int>(inputImg.cols);
	height = static_cast<int>(inputImg.rows);
	int height_crop = height - 249;
	// fps = static_cast<int>(video.get(CAP_PROP_FPS));
	// fourcc = static_cast<int>(video.get(CAP_PROP_FOURCC));

	cout << "Input image: (" << width << "x" << height << ")" << endl;
	
	// The 4-points at the input image	
	vector<Point2f> origPoints;
	origPoints.push_back( Point2f(0, height_crop) );
	origPoints.push_back( Point2f(width, height_crop) );
	origPoints.push_back( Point2f(width/2 + 50, 0) );
	origPoints.push_back( Point2f(width/2 - 30, 0) );

	// The 4-points correspondences in the destination image
	vector<Point2f> dstPoints;
	dstPoints.push_back( Point2f(width/2-50, height) );
	dstPoints.push_back( Point2f(width/2+100, height) );
	dstPoints.push_back( Point2f(width/2 + 50, 0) );
	dstPoints.push_back( Point2f(width/2 - 30, 0) );

		
	// IPM object
	IPM ipm( Size(width, height), Size(width, height), origPoints, dstPoints );

	// Color Conversion
	if(inputImg.channels() == 3)
		cvtColor(inputImg, inputImgGray, cv::COLOR_BGR2GRAY);				 		 
	else
		inputImg.copyTo(inputImgGray);			 		 

	// Process
	clock_t begin = clock();
	ipm.applyHomography( inputImg, outputImg );		 
	clock_t end = clock();
	double elapsed_secs = double(end - begin) / CLOCKS_PER_SEC;
	printf("%.2f (ms)\r", 1000*elapsed_secs);
	ipm.drawPoints(origPoints, inputImg );

	// View		
	imshow("Input", inputImg);
	imshow("Output", outputImg);
	waitKey(0);












	
	// // Main loop
	// int frameNum = 0;
	// for( ; ; )
	// {
	// 	// printf("FRAME #%6d ", frameNum);
	// 	fflush(stdout);
	// 	frameNum++;

	// 	// Get current image		
	// 	video >> inputImg;
	// 	if( inputImg.empty() )
	// 		break;

	// 	 // Color Conversion
	// 	 if(inputImg.channels() == 3)		 
	// 		 cvtColor(inputImg, inputImgGray, cv::COLOR_BGR2GRAY);				 		 
	// 	 else	 
	// 		 inputImg.copyTo(inputImgGray);			 		 

	// 	 // Process
	// 	 clock_t begin = clock();
	// 	 ipm.applyHomography( inputImg, outputImg );		 
	// 	 clock_t end = clock();
	// 	 double elapsed_secs = double(end - begin) / CLOCKS_PER_SEC;
	// 	 // printf("%.2f (ms)\r", 1000*elapsed_secs);
	// 	 ipm.drawPoints(origPoints, inputImg );

	// 	 // View		
	// 	 imshow("Input", inputImg);
	// 	 imshow("Output", outputImg);
	// 	 waitKey(0);
	// }

	return 0;	
}		
