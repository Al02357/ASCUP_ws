#include <iostream>
#include <vector>
#include <Eigen/Eigen>

#include <ros/ros.h>
#include <cmath>
#include <geometry_msgs/PoseStamped.h>
#include <geographic_msgs/GeoPoseStamped.h>
#include <mavros_msgs/CommandBool.h>
#include <mavros_msgs/SetMode.h>
#include <mavros_msgs/State.h>
#include <sensor_msgs/BatteryState.h>
#include <geometry_msgs/TwistStamped.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <thread>
#include <eigen3/Eigen/Dense>
#include <std_msgs/Float64MultiArray.h>
#include <complex.h>
using namespace std;
int main(){
    Eigen::Matrix3i test;
                                                test<< 1,2,3,
                                                4,5,6,
                                                7,8,9;
    for(int i = 0;i<9;i++){
        std::cout<<test(i)<<std::endl;
    }
    for(int i = 0;i<3;i++){
        for(int j = 0;j<3;j++){
            std::cout<<test(i,j)<<std::endl;       
             }
    }
    std::complex<double> cm1(1,1);  
    std::complex<double> cm2(-1,1);  
    std::complex<double> cm3(-1,-1);  
    std::complex<double> cm4(1,-1);  

    double arg_1 = arg(cm1);
    double arg_2 = arg(cm2);
    double arg_3 = arg(cm3);
    double arg_4 = arg(cm4);

    std::cout<<"----"<<std::endl;
    std::cout<<arg_1<<std::endl;
    std::cout<<arg_2<<std::endl;
    std::cout<<arg_3<<std::endl;
    std::cout<<arg_4<<std::endl;
    cout<<"----"<<endl;


    // cout<<geo_q1<<endl;
    // cout<<geo_q2<<endl;
    // cout<<geo_q3<<endl;
    // cout<<geo_q4<<endl;
    return 0;
}