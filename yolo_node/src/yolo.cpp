﻿/*
    This file is for the famous YOLO object detection executed in ROS.

    YOLO_ROS_PLUGIN is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    YOLO_ROS_PLUGIN is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with YOLO_ROS_PLUGIN.  If not, see <http://www.gnu.org/licenses/>.
*/

/**
 * \file yolo.cpp
 * \date 01/07/2022
 * \author pattylo
 * \copyright (c) AIRO-LAB, RCUAS of Hong Kong Polytechnic University
 * \brief //legacy of AUTO (https://github.com/HKPolyU-UAV/AUTO) & previous 1st gen ALAN (https://www.mdpi.com/1424-8220/22/1/404)
 */

#include "include/yolo.h"


void mission_control_center::CnnNodelet::color_image_raw_callback(
    const sensor_msgs::ImageConstPtr &rgbmsg
)
{
    if(!initiated)
        return;
    
    // main process here:
    cv_bridge::CvImageConstPtr rgb_ptr;
    try
    {
        rgb_ptr  = cv_bridge::toCvCopy(rgbmsg, sensor_msgs::image_encodings::BGR8);
    }
    catch (cv_bridge::Exception& e)
    {
        ROS_ERROR("cv_bridge exception: %s", e.what());
    }

    frame = rgb_ptr->image;
    
    rundarknet(this->frame);  
    set_image_to_publish();  
}

void mission_control_center::CnnNodelet::color_image_compressed_callback(
    const sensor_msgs::CompressedImageConstPtr &rgbmsg
)
{
    if(!initiated)
        return;
    
    // main process here:
    try
    {
        frame = cv::imdecode(cv::Mat(rgbmsg->data), 1);
    }
    catch (cv_bridge::Exception& e)
    {
        ROS_ERROR("cv_bridge exception: %s", e.what());
    }

    rundarknet(this->frame); 
    set_image_to_publish();   
}

void mission_control_center::CnnNodelet::color_depth_image_raw_callback(
    const sensor_msgs::ImageConstPtr & rgbmsg, 
    const sensor_msgs::ImageConstPtr & depthmsg
)
{
    cv_bridge::CvImageConstPtr depth_ptr;

    try
    {
        depth_ptr  = cv_bridge::toCvCopy(depthmsg, depthmsg->encoding);
    }
    catch (cv_bridge::Exception& e)
    {
        ROS_ERROR("cv_bridge exception: %s", e.what());
        return;
    }

    cv::Mat depth = depth_ptr->image;

    cv_bridge::CvImageConstPtr rgb_ptr;
    try
    {
        rgb_ptr  = cv_bridge::toCvCopy(rgbmsg, sensor_msgs::image_encodings::BGR8);
    }
    catch (cv_bridge::Exception& e)
    {
        ROS_ERROR("cv_bridge exception: %s", e.what());
    }

    frame = rgb_ptr->image;

    getdepthdata(depth);
    rundarknet(frame);
    set_image_to_publish();
}

void mission_control_center::CnnNodelet::color_depth_image_compressed_callback(
    const sensor_msgs::CompressedImageConstPtr & rgbmsg, 
    const sensor_msgs::ImageConstPtr & depthmsg
)
{
    cv_bridge::CvImageConstPtr depth_ptr;

    try
    {
        depth_ptr  = cv_bridge::toCvCopy(depthmsg, depthmsg->encoding);
    }
    catch (cv_bridge::Exception& e)
    {
        ROS_ERROR("cv_bridge exception: %s", e.what());
        return;
    }

    // std::cout<<1<<std::endl;

    cv::Mat depth = depth_ptr->image;

    try
    {
        frame = cv::imdecode(cv::Mat(rgbmsg->data), 1);
    }
    catch (cv_bridge::Exception& e)
    {
        ROS_ERROR("cv_bridge exception: %s", e.what());
    }

    getdepthdata(depth);
    rundarknet(frame);
    set_image_to_publish();
}

void mission_control_center::CnnNodelet::set_image_to_publish()
{
    cv_bridge::CvImage for_visual;
    for_visual.header.stamp = ros::Time::now();
    for_visual.encoding = sensor_msgs::image_encodings::BGR8;
    for_visual.image = frame.clone();
    this->pubimage.publish(for_visual.toImageMsg());

}

inline void mission_control_center::CnnNodelet::CnnNodeletInitiate(const cv::String cfgfile, const cv::String weightfile, const cv::String classnamepath)
{
    std::cout<<"start initiation..."<<std::endl;
    this->mydnn = cv::dnn::readNetFromDarknet(cfgfile, weightfile);

    //opt CUDA or notcc
    
    if(w_GPU)
    {
        this->mydnn.setPreferableBackend(cv::dnn::DNN_BACKEND_CUDA);
        this->mydnn.setPreferableTarget(cv::dnn::DNN_TARGET_CUDA);
    }
    else
    {
        this->mydnn.setPreferableBackend(cv::dnn::DNN_BACKEND_DEFAULT);
        this->mydnn.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
    }

    initiated = true;

    std::cout<<"end   initiation..."<<std::endl;

}

void mission_control_center::CnnNodelet::rundarknet(cv::Mat &frame)
{
    this->obj_vector.clear();
    this->total_start = std::chrono::steady_clock::now();
    findboundingboxes(frame);
    this->total_end = std::chrono::steady_clock::now();
    total_fps = 1000.0 / std::chrono::duration_cast<std::chrono::milliseconds>(total_end - total_start).count();
    this->appro_fps = total_fps;
}

void mission_control_center::CnnNodelet::display(cv::Mat frame)
{
    cv::imshow("Yolo-ed", frame);
    cv::waitKey(10);
}

void mission_control_center::CnnNodelet::getdepthdata(cv::Mat depthdata)
{
    this->depthdata = depthdata;
}

void mission_control_center::CnnNodelet::findboundingboxes(cv::Mat &frame)
{
    cv::Mat blob;
    using namespace std;

    blob = cv::dnn::blobFromImage(frame, 0.00392, cv::Size(CNN_size, CNN_size), cv::Scalar(), true, false);
    //    as the dnn::net function does not accept image from image, it only receive blob hence the above function, refer to teams/article/blob

    mydnn.setInput(blob);
    //feed 4-D blob to darknet dnn

    std::vector<cv::String> net_outputNames;//names of output layer of yolo, should be 3
    net_outputNames = mydnn.getUnconnectedOutLayersNames();

    std::vector<cv::Mat> netOutput;
    //coppy the result to this object


    double starttime = ros::Time::now().toSec();

    mydnn.forward(netOutput, net_outputNames);

    //    std::cout<<netOut[0].size()<<std::endl;
    //    std::cout<<netOut[1].size()<<std::endl;
    //    std::cout<<netOut[2].size()<<std::endl<<std::endl;
    double endtime = ros::Time::now().toSec();
    double deltatime = endtime - starttime;
    // std::cout<<"time:"<<deltatime<<std::endl;
    // std::cout<<"fps: "<<1/deltatime<<std::endl;
    findwhichboundingboxrocks(netOutput, frame);
}

void mission_control_center::CnnNodelet::findwhichboundingboxrocks(std::vector<cv::Mat> &netOutput, cv::Mat &frame)
{
    std::vector<float> confidenceperbbox;
    std::vector<int> indices;
    std::vector<cv::Rect> bboxes;
    std::vector<std::string> classnames;
    std::vector<int> classids;

    getclassname(classnames);

    int indicator =0;
    for(auto &output: netOutput)//read every layer's output, the auto variable of "output" indicates 3 different layer, as per the architecture of yolo
    {
        for(int i=0;i<output.rows;i++)//now, for every layer's output, there will be 17328*(5+class number), 4332*(5+class number), 1083*(5+class number) numbers, it holds the very info of every predicted bounding boxes
        {
            auto isthereanobjectconfidence = output.at<float> (i,4);//save the confidence of every bounding box
            if(isthereanobjectconfidence>set_confidence)//this does: assess whether there is an object in this bounding box
            //if there is, further extract the data
            {
                auto x =output.at<float>(i,0) * frame.cols;
                auto y =output.at<float>(i,1) * frame.rows;
                auto w =output.at<float>(i,2) * frame.cols;
                auto h =output.at<float>(i,3) * frame.rows;
                //                auto c_max =output.at<float>(i,4);
                //                auto c_no1 =output.at<float>(i,5);
                //                auto c_no2 =output.at<float>(i,6);
                //                auto c_no3 =output.at<float>(i,7);
                //                auto c_no4 =output.at<float>(i,8);
                //                auto c_no5 =output.at<float>(i,9);
                auto x_ = int(x - w/2);
                auto y_ = int(y - h/2);
                auto w_ = int(w);
                auto h_ = int(h);
                cv::Rect Rect_temp(x_,y_,w_,h_);

                //                std::cout<<"here: "<<c_max<<" "<<c_no1<<" "<<c_no2<<" "<<c_no3<<" "<<c_no4<<" "<<c_no5<<" "<<std::endl<<std::endl;
                for(int class_i=0;class_i<classnames.size();class_i++)//as for this step, this for loop take the probabilities of every class
                {
                    auto confidence_each_class = output.at<float>(i, 5+class_i); //6th element will be the 1st class confidence, class id=0
                        //7th element will be the 2nd class confidence, class id=1, etc
                    if(confidence_each_class>set_confidence)
                    {
                        bboxes.push_back(Rect_temp);
                        confidenceperbbox.push_back(confidence_each_class);
                        classids.push_back(class_i);
                    }
                }
            }
        }
    }

    cv::dnn::NMSBoxes(bboxes,confidenceperbbox,0.1,0.1,indices);
    //Basically, the indicies return the index of the bboxes, i.e, show which bounding box is the most suitable one
    for(int i =0 ; i < indices.size();i++)
    {
        int index = indices[i];

        int final_x, final_y, final_w, final_h;
        final_x = bboxes[index].x;
        final_y = bboxes[index].y;
        final_w = bboxes[index].width;
        final_h = bboxes[index].height;
        cv::Scalar color;
        cv::Point center = cv::Point(final_x+final_w/2, final_y+final_h/2);

        // confidence
        std::string detectedclass = classnames[classids[index]];
        float detectedconfidence = confidenceperbbox[index]*100;
        char temp_confidence[40];
        sprintf(temp_confidence, "%.2f", detectedconfidence);  


        // depth info here
        double depthofboundingbox = -INFINITY;
        char temp_depth[40];

        if(input_type == 2 || input_type == 3)
        {
            int depthbox_w = final_w*0.25;
            int depthbox_h = final_h*0.25;

            cv::Point depthbox_vertice1 = cv::Point(center.x - depthbox_w/2, center.y - depthbox_h/2);
            cv::Point depthbox_vertice2 = cv::Point(center.x + depthbox_w/2, center.y + depthbox_h/2);
            cv::Rect letsgetdepth(depthbox_vertice1, depthbox_vertice2);

            cv::Mat ROI(depthdata, letsgetdepth);
            cv::Mat ROIframe;
            ROI.copyTo(ROIframe);
            std::vector<cv::Point> nonzeros;

            cv::findNonZero(ROIframe, nonzeros);
            std::vector<double> nonzerosvalue;
            for(auto temp : nonzeros)
            {
                double depth = ROIframe.at<ushort>(temp);
                nonzerosvalue.push_back(depth);
            }

            double depth_average;
            if(nonzerosvalue.size()!=0)
                depth_average = accumulate(nonzerosvalue.begin(), nonzerosvalue.end(),0.0)/nonzerosvalue.size();
                


            cv::Point getdepth(final_x+final_w/2, final_y+final_h/2);
            depthofboundingbox = 0.001 * depth_average;

            
            sprintf(temp_depth, "%.2f", depthofboundingbox);

        }
        else
        {
            sprintf(temp_depth, "%.2f", -1.0);
            depthofboundingbox = -1.0;
            
        }
            

           

        // post-processing

        std::string textoutputonframe = detectedclass + ": " + temp_confidence + "%, "+ temp_depth + "m";
        cv::Scalar colorforbox(rand()&255, rand()&255, rand()&255);
        
        
        

        cv::rectangle(frame, cv::Point(final_x, final_y), cv::Point(final_x+final_w, final_y+final_h), colorforbox,2);
        cv::putText(frame, textoutputonframe, cv::Point(final_x,final_y-10),cv::FONT_HERSHEY_COMPLEX_SMALL,1,CV_RGB(255,255,0));
        obj.confidence = detectedconfidence;
        obj.classnameofdetection = detectedclass;
        obj.boundingbox = cv::Rect(cv::Point(final_x, final_y), cv::Point(final_x+final_w, final_y+final_h));
        obj.depth = depthofboundingbox;
        obj.frame = frame;
        obj_vector.push_back(obj);
    }

    char hz[40];
    char fps[5] = " fps";
    sprintf(hz, "%.2f", this->appro_fps);
    strcat(hz, fps);
    cv::putText(frame, hz, cv::Point(20,40), cv::FONT_HERSHEY_PLAIN, 1.6, CV_RGB(255,0,0));

    std::string image_size_text = "image_size";
    std::string image_size_put_on_frame = 
        image_size_text + ": " 
        + std::to_string(frame.rows)
        + " X "
        + std::to_string(frame.cols);
    
    cv::putText(frame, image_size_put_on_frame, cv::Point(20,80), cv::FONT_HERSHEY_PLAIN, 1.6, CV_RGB(255,0,0));

    if(input_type == 0 || input_type == 1)
    {
        cv::putText(frame, "no_depth!", cv::Point(20,120), cv::FONT_HERSHEY_PLAIN, 1.6, CV_RGB(255,0,0));
    }

}

void mission_control_center::CnnNodelet::getclassname(std::vector<std::string> &classnames)
{
    std::ifstream class_file(classnamepath);
    if (!class_file)
    {
        std::cerr << "failed to open classes.txt\n";
    }

    std::string line;
    while (getline(class_file, line))
    {
        classnames.push_back(line);
    }
}