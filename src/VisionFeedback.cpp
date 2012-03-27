#include <iostream>
#include <vector>
#include <queue>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <ros/ros.h>

#include <CoaxVisionControl.h>
#include <coax_vision/ImageDebug.h>

namespace enc = sensor_msgs::image_encodings;

bool CompareSymAxis::operator() (SymAxis& A1, SymAxis& A2) 
{
	if (A1.value < A2.value)
		return true;
	else
		return false;
};

VisionFeedback::VisionFeedback(ros::NodeHandle& nh_)
:it_(nh_) 
,Sub_Image(it_.subscribe("image_in", 1 ,&VisionFeedback::proc, this))
,Pub_Image(it_.advertise("image_proc", 1))
,Debug_Msgs(nh_.advertise<coax_vision::ImageDebug>("image_debug",100))
,FIRST_FRAME(true)
,width(0)
,height(0)
,symPos(0)
,shift(0)
{
}

VisionFeedback::~VisionFeedback() {
}

void VisionFeedback::proc(const sensor_msgs::ImageConstPtr& msg) 
{
//	ROS_INFO("frame type %d",msg->step);
	sensor_msgs::Image frame;
	frame = *msg;
	// Resize
	size_t resize = 10; 
	frame.height = msg->height / resize;
  frame.width = msg->width / resize;
  frame.step = msg->width / resize;
	frame.data.resize(frame.height * frame.width);
  size_t im_idx = 0;
  for (size_t i = 0; i < msg->height; i += resize)
	  for (size_t j = 0; j < msg->width; j += resize) {
			frame.data[im_idx] = msg->data[i*msg->step+j];
			im_idx ++;
		}

	// Inegral Image
	std::vector <uint32_t> Integral_Image;
	size_t Idx = 0;          
	size_t Up_Idx = 0;       
	size_t Left_Idx = 0;     
	size_t UpLeft_Idx = 0;   
	uint8_t Up = 0;                
	uint8_t Left = 0;              
	uint8_t UpLeft = 0;            
	Integral_Image.resize(frame.height * frame.width);
	for (size_t i = 0; i < frame.width; i++)
  	for (size_t j = 0; j < frame.height; j++) {
			Idx = j * frame.width + i;
			Up_Idx = (j-1) * frame.width + i;
			Left_Idx = j * frame.width + i - 1;
			UpLeft_Idx = (j-1) * frame.width + i - 1;
			Up = (j == 0)? 0 : frame.data[Up_Idx];
			Left = (i == 0)? 0 : frame.data[Left_Idx];
			UpLeft = ((i == 0) || (j == 0))? 0 : frame.data[UpLeft_Idx];
			Integral_Image[Idx] = frame.data[Idx] + Up + Left - UpLeft;  
  }

	std::priority_queue<SymAxis, std::vector<SymAxis>, CompareSymAxis> Axis;
	std::vector<SymAxis> AxisErr;
/*	
	// Integral Image Based kernel scan
	size_t kerSize = 24; // Based on Matlab Result
	size_t Aidx,Bidx,Cidx,Didx;
	double A,B,C,D,LeftSum,RightSum;
	double accDiff = 0;
	for (size_t col = 0; col < (frame.width - kerSize); col++) {
		accDiff = 0;
		for (size_t row = 0; row < (frame.height - kerSize); row++) {
			// Left part
			Aidx = (row-1) * frame.width + (col-1);
			Bidx = (row-1) * frame.width + (col+kerSize/2-1);
			Cidx = (row+kerSize-1) * frame.width + (col+kerSize/2-1);
			Didx = (row+kerSize-1) * frame.width + (col-1);
			A = ((row == 0) || (col == 0))? 0 : Integral_Image[Aidx];
			B = (row == 0)? 0 : Integral_Image[Bidx];
			C = Integral_Image[Cidx];
			D = (col == 0)? 0 : Integral_Image[Didx];
			LeftSum = A + C - B - D;
			// Right part
			Aidx = Bidx;
			Didx = Cidx;
			Bidx = (row-1) * frame.width + (col+kerSize-1);
			Cidx = (row+kerSize-1) * frame.width + (col+kerSize-1);
			A = (row == 0)? 0 : Integral_Image[Aidx];
			B = (row == 0)? 0 : Integral_Image[Bidx];
			C = Integral_Image[Cidx];
			D = Integral_Image[Didx];
			RightSum = A + C - B - D;
			accDiff += abs(LeftSum - RightSum);
		}
		SymAxis Temp = {col+1,accDiff/frame.height};
		Axis.push(Temp);
		AxisErr.push_back(Temp);
	}
*/

	// optic flow
	std::vector<double> OpticFlow;
	double heightSum = 0;
	for (size_t col = 0; col < frame.width; col++) {
		heightSum = 0;
		for (size_t row = 0; row < frame.height; row++) {
			heightSum += frame.data[row * frame.width + col];	
		}
		OpticFlow.push_back(heightSum/frame.height);
	}
	
	double leftshift, rightshift;
	double sumNom, sumDenom;
	double shifitsize = 1;
	if (FIRST_FRAME) {
		shift = 0;
		LastOpicFlow = OpticFlow;
		FIRST_FRAME = false;
	}
	else {
		sumNom = 0;
		sumDenom = 0;
		for (size_t iter = 0; iter < OpticFlow.size(); iter++) {
			leftshift = ((iter-shifitsize)>0)? LastOpicFlow[iter-shifitsize] : 0;
			rightshift = ((iter+shifitsize)<OpticFlow.size())? LastOpicFlow[iter+shifitsize] : 0;
			sumNom += (OpticFlow[iter]-LastOpicFlow[iter]) * (leftshift-rightshift);
			sumDenom += pow(leftshift-rightshift,2);
		}
		shift = 2 * shifitsize * sumNom / sumDenom;
	}
//	ROS_INFO("Lateral Shift %f",shift);
	//
	// Integral Image Based, abs(sum(left)-sum(right))
//	size_t shift = (frame.height - 1) * frame.width;
//	double Left_Sum = 0;
//	double Right_Sum = 0;
//	for (unsigned short i = 1; i < (frame.width - 1); i++) {
//		if ( i < frame.width/2 ) {
//			Left_Sum = Integral_Image[shift+(i-1)];
//			Right_Sum = Integral_Image[shift+2*i] - Integral_Image[shift+i];
//		}
//		else {
//			Left_Sum = Integral_Image[shift+i-1] - Integral_Image[shift+2*i-frame.width];
//			Right_Sum = Integral_Image[shift+frame.width-1] - Integral_Image[shift+i];
//		}
//		SymAxis temp_axis = {i,abs(Left_Sum-Right_Sum)};
//		Axis.push(temp_axis);
//	}

	// Kernel based , L1
	size_t width = 0;
	double Sum_L1_Norm = 0;	
	size_t shift = 0;
	size_t Left_Cur = 0;
	size_t Right_Cur = 0; 
	double L1_Norm = 0;
	for (size_t cnt = 1; cnt < (frame.width - 1); cnt++) {
		width = std::min(cnt,frame.width-1-cnt);
		Sum_L1_Norm = 0;
		for (size_t row = 0; row < frame.height; row++) {
			shift = row * frame.width;
			for (size_t cur = 1; cur <= width; cur++) {
				Left_Cur = cnt - cur;
				Right_Cur = cnt + cur;
				L1_Norm = abs(frame.data[shift+Left_Cur] - frame.data[shift+Right_Cur]);
				Sum_L1_Norm += L1_Norm;
			}
		}
		SymAxis temp_axis = {cnt, Sum_L1_Norm};
		Axis.push(temp_axis);
	}
	
//			
//	SymAxis Best = Axis.top();
//	Axis.pop();
//	SymAxis Second = Axis.top();
//	Axis.pop();
//	SymAxis Third = Axis.top();
	//ROS_INFO("Axis %d",Best.axis);
	
	SortedAxis.clear();
	while (!Axis.empty()) {
		SortedAxis.push_back(Axis.top());
		Axis.pop();
	}
	
//	deque<SymAxis> PeakA;		
//	for (size_t iter = 1; iter < AxisErr.size()-1; iter++) {
//		if ((AxisErr[iter].value > AxisErr[iter-1].value) && 
//				(AxisErr[iter].value > AxisErr[iter+1].value)) {
//			PeakA.push_back(AxisErr[iter]);	
//		}
//	}
//	PeakAxis = PeakA;
	// Generate Debug Message
	coax_vision::ImageDebug debugMsg;
	debugMsg.header.stamp = ros::Time::now();
	debugMsg.header.seq = 0;
	debugMsg.header.frame_id = "image_debug";
//	debugMsg.BestAxis = Best.axis;
//	debugMsg.SecondAxis = Second.axis;
//	debugMsg.ThirdAxis = Third.axis;
	Debug_Msgs.publish(debugMsg);	
	Pub_Image.publish(frame);
}

