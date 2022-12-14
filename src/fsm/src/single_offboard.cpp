/**
 * @file offb_node.cpp
 * @brief Offboard control example node, written with MAVROS version 0.19.x, PX4 Pro Flight
 * Stack and tested in Gazebo SITL
 */

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
#include <fsm/command_acc.h>
#include <bspline_race/bspline_race.h>
#include <complex.h>
using namespace std;

static mavros_msgs::State current_state;
static int flight_mode=1;
static int waypoint_num=0;
static int j=1;
static int cmdd=0;
static int flag1=0;
static int flag2=0;
static int ready_fly=0;
static float voltage_now;
static double waypoint[50][3];
static geometry_msgs::PoseStamped aim;
static geometry_msgs::PoseStamped goal;
Eigen::Vector3d pos_drone_fcu;
Eigen::Vector3d pos_drone_now;
Eigen::Vector2d yaw_drone_fcu;


/*----------ASCUP----------*/
static int flag_A = 0;                              // 0: disable mission A debug       1: enable mission A debug
static int flag_B = 0;                              // 0: disable mission B debug       1: enable mission B debug
static int flag_C = 0;                              // 0: disable mission C debug       1: enable mission C debug
static int flag_D = 0;                              // 0: disable mission D debug       1: enable mission D debug
static int flag_E = 0;                              // 0: disable mission E debug       1: enable mission E debug
static int flag_LAND = 1;                           // 0: disable mission land debug    1: enable mission land debug  

int flag_a = 0;                           /* 0: start state
                                             1: arrive point 1 (find bounding box)
                                             2: arrive point 2 (bounding box right midpoint) 
                                             3: arrive point 3 (bounding box left midpoint)  
                                             4: arrive point 4 (transition before mission B)     */
int count_box = 0;
Eigen::Vector3d box_midpoint;
double box_width;

int flag_land = 0;                           /* 0: start state: determine wait coordinate
                                               1: wait recognition 
                                               2: determine tag coordinate
                                               3: fly towards tag         
                                               4: land                     */ 
int flag_tag_recog = 0;
int count_tag = 0;
Eigen::Vector3d pos_tag;
Eigen::Vector3d pos_land;                

static int flag_ring1 = 2;
static int flag_ring2 = 0;
static int flag_ring3 = 0;
static int flag_ring4 = 0;

int flag_ring_recog = 0;
int count_ring1 = 0;
int count_bs = 0;
bool jam = 0;
Eigen::Vector3d ring_center;                                //center point from ring recognition
Eigen::Vector3d ring_direct;                                //normal vector from ring recognition
Eigen::Vector3d ring_before;                                //point before going through ring      
Eigen::Vector3d ring_after;                                 //point after going through ring    
Eigen::Vector2d ring_orientation;                           //yaw orientation to go through ring

bspline_race::BsplineTraj traj_fsm;
vector<Eigen::Vector2d> traj;
// geometry_msgs::PoseStamped[] traj_pos;
// geometry_msgs::PoseStamped[] traj_vel;
// geometry_msgs::PoseStamped[] traj_acc;
int traj_len;
int traj_p = 0;
int flag_new_traj = 1;

/*-------------------------*/

/*----------ASCUP----------*/
void box_cb(const std_msgs::Float64MultiArray::ConstPtr& msg)
{
    if(count_box < 10)
    {
        box_midpoint = Eigen::Vector3d(msg->data[0], msg->data[1], 0.6);
        box_width = msg->data[3];
        
        /*----------check bounding box width: error < 0.3 as correct----------*/
        if(abs(box_width - 2.5) < 0.3) {count_box = count_box + 1;} 
    }
}

void ring_cb(const geometry_msgs::PoseStamped::ConstPtr &msg)
{
    ring_center = Eigen::Vector3d(msg->pose.position.x, msg->pose.position.y, msg->pose.position.z);
    ring_direct = Eigen::Vector3d(msg->pose.orientation.z, -msg->pose.orientation.x, -msg->pose.orientation.y);
    flag_ring_recog = 1;
}

void tag_cb(const geometry_msgs::PoseStamped::ConstPtr &msg)
{
    pos_tag = Eigen::Vector3d(msg->pose.position.z, -msg->pose.position.x, -msg->pose.position.y);
    flag_tag_recog = 1;
}

void traj_cb(const bspline_race::BsplineTraj::ConstPtr& msg)
{
    jam= 1;
    // count_bs++;
    // if(flag_new_traj == 1) 
    // {
        // traj_fsm = *(msg);
        
        // traj_fsm.position = msg->velocity;
        // traj_acc = msg->acceleration;
        int iter;
        for(int i = 0;i<msg->position.size();i++)
        {
            double error;
            error = sqrt(pow((msg->position[i].pose.position.x-pos_drone_fcu[0]),2)+pow((msg->position[i].pose.position.y-pos_drone_fcu[1]),2));
            if(error<=0.05)
            {
                iter = i;
                cout<<"near:" <<error<<endl;
                break;
                
            }
            else{
                Eigen::Vector2d ptr;
                ptr << msg->position[i].pose.position.x,msg->position[i].pose.position.y;
                cout<<"get i = "<<i<<"with error: "<<error<<endl;
                traj.push_back(ptr);
            }
        }
        

        // geometry_msgs::PoseStamped ptr;
        // auto iter_ = traj_fsm.position.begin();
        // for(auto iter = traj_fsm.position.begin();iter!=traj_fsm.position.end();iter++)
        // {
        //     ptr = *(iter);
        //     double error;
        //     error = sqrt(pow((ptr.pose.position.x-pos_drone_fcu[0]),2)+pow((ptr.pose.position.y-pos_drone_fcu[1]),2));
        //     if(error<=0.05)
        //     {
        //         iter_ = iter;
        //         break;
        //     }
        // }
        // if(iter_!=traj_fsm.position.end()-1){
        //     for(auto iter = iter_;iter!=traj_fsm.position.end();iter++)
        //     {
        //         traj_fsm.position.erase(iter);
        //     }
        // }
        


        traj_len = msg->position.size();
        cout<< "~~~~~~~~~~~~~traj_len: "<<traj_len<<"    ~~~~~~~~~~~~~~~~~~~~~~~"<<endl;
        traj_p =  iter - 1;                                                //???
         traj_len = traj.size();
        cout<< "~~~~~~~~~~~~~traj_len: "<<traj_len<<"    ~~~~~~~~~~~~~~~~~~~~~~~"<<endl;
    // }

    // flag_new_traj = 0;
    // if(count_bs == 60){
    //     count_bs = 0;
    //     flag_new_traj = 1;
    // }
    jam = 0;
}

/*-------------------------*/



void vtg_cb(const sensor_msgs::BatteryState::ConstPtr &msg)
{
    voltage_now = msg->voltage;
}

void ready_cb(const fsm::command_acc &msg)
{ 
    ready_fly=msg.ready;
    //ROS_ERROR("11111111");voltage_now
    //cout<<"1111";
}

void state_cb(const mavros_msgs::State::ConstPtr& msg)
{
    current_state = *msg;
}

void pos_cb(const geometry_msgs::PoseStamped::ConstPtr &msg)
{
    pos_drone_fcu = Eigen::Vector3d(msg->pose.position.x, msg->pose.position.y, msg->pose.position.z);
    yaw_drone_fcu = Eigen::Vector2d(msg->pose.orientation.x, msg->pose.orientation.w);
}

void UdpListen(const uint16_t cport)
{
    ros::NodeHandle nh;
    int sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if(sock_fd < 0)
    {
        ROS_ERROR("Network Error");
        return;
    }

    /* ???????????????IP??????????????? */
    struct sockaddr_in addr_lis;
    int len;
    memset(&addr_lis, 0, sizeof(struct sockaddr_in));
    addr_lis.sin_family = AF_INET;
    addr_lis.sin_port = htons(cport);
    /* INADDR_ANY??????????????????????????????????????????????????????????????????SERV_PORT???????????????????????????????????? */
    addr_lis.sin_addr.s_addr = htonl(INADDR_ANY);  //????????????IP??????
    len = sizeof(addr_lis);

    /* ??????socket */
    if(bind(sock_fd, (struct sockaddr *)&addr_lis, sizeof(addr_lis)) < 0)
    {
      perror("bind error:");
      exit(1);
    }


    int recv_num;
    char recv_buf[100];
    const char dot[2] = ",";
    struct sockaddr_in addr_client;

    while(ros::ok()){
        char *p;
        int ent=0;
        int cmd;
        double px,py,pz;

        recv_num = recvfrom(sock_fd, recv_buf, sizeof(recv_buf), 0, (struct sockaddr *)&addr_client, (socklen_t *)&len);

        if(recv_num < 0||abs(recv_num-19)>3)
        {
            ROS_ERROR("Recv Fail!");
            continue;
        }
        recv_buf[recv_num] = '\0';
//      ROS_INFO("Rec: %s, len=%d",recv_buf,recv_num);

        p = strtok(recv_buf,dot);
        sscanf(p,"%d",&cmd);
        p=strtok(NULL,dot);
        sscanf(p,"%lf",&px);
        p=strtok(NULL,dot);
        sscanf(p,"%lf",&py);
        p=strtok(NULL,dot);
        sscanf(p,"%lf",&pz);
        if(cmd!=6)
        {
          aim.pose.position.x=px;
          aim.pose.position.y=py;
          aim.pose.position.z=pz;
                    complex<double> cm(0.0,-1);  
                    double arg_ = arg(cm);
                    geometry_msgs::Quaternion geo_q = tf::createQuaternionMsgFromYaw(arg_);
                    aim.pose.orientation = geo_q;
        }
        ROS_INFO("Rec: %d, %.3lf, %.3lf, %.3lf",cmd,aim.pose.position.x,aim.pose.position.y,aim.pose.position.z);
        if(flag2==0)
        cmdd=cmd;
         switch (cmdd)
        {
        case 0:
            if(ready_fly==1)
            cout<<"Ready Fly"<<endl;
            else
            ROS_INFO("Waiting");
            break;
            
        case 1:
            if(pos_drone_fcu[2]>1.5||voltage_now<13.5)
            {
            cmdd=4;
            flag2=1;
            }
            break;
        case 9:
            if(pos_drone_fcu[2]>1.5||voltage_now<13.5)
            {
            cmdd=4;
            flag2=1;
            }
            break;
            
        }
        //   cout<<voltage_now<<endl;
        //    cout<<pos_drone_fcu[2]<<endl;
    }

}



int main(int argc, char **argv)
{
    ros::init(argc, argv, "offboard");
    ros::NodeHandle nh;

    ros::Subscriber state_sub = nh.subscribe<mavros_msgs::State>   
            ("mavros/state", 10, state_cb);
    ros::Subscriber position_sub = nh.subscribe<geometry_msgs::PoseStamped>
            ("/mavros/local_position/pose", 100, pos_cb);
    ros::Subscriber ready= nh.subscribe("/px4/ready",10,ready_cb);
    ros::Subscriber voltage_sub = nh.subscribe<sensor_msgs::BatteryState>
            ("/mavros/battery", 300, vtg_cb);

    /*----------AISHENG CUP----------*/
    ros::Subscriber box_sub = nh.subscribe<std_msgs::Float64MultiArray>
            ("/task_A_msgs", 100, box_cb);
    ros::Subscriber ring_sub = nh.subscribe<geometry_msgs::PoseStamped>
            ("/orb_pose", 100, ring_cb);
    ros::Subscriber tag_sub = nh.subscribe<geometry_msgs::PoseStamped>
            ("/apriltag_ros_continuous_node/apriltag_pose", 100, tag_cb);
    ros::Subscriber traj_sub = nh.subscribe<bspline_race::BsplineTraj>
            ("/bspline_traj", 10 ,traj_cb);

    ros::Publisher traj_goal_pub = nh.advertise<geometry_msgs::PoseStamped>
            ("/move_base_simple/goal", 100);
    
    /*-------------------------------*/

    ros::ServiceClient arming_client = nh.serviceClient<mavros_msgs::CommandBool>  
            ("mavros/cmd/arming");
    ros::ServiceClient set_mode_client = nh.serviceClient<mavros_msgs::SetMode>   
            ("mavros/set_mode");

    ros::Publisher local_pos_pub = nh.advertise<geometry_msgs::PoseStamped>    
            ("mavros/setpoint_position/local", 10);

    //the setpoint publishing rate MUST be faster than 2Hz
    ros::Rate rate(20.0);
    //parameters
    nh.param("/single_offboard/way_num",waypoint_num,0);
    aim.pose.position.x = 0;
    aim.pose.position.y = 0;
    aim.pose.position.z = 0.3;
                    complex<double> cm(0.0,-1);  
                    double arg_ = arg(cm);
                    geometry_msgs::Quaternion geo_q = tf::createQuaternionMsgFromYaw(arg_);
                    aim.pose.orientation = geo_q;
    //point
    for (int i = 1; i <= waypoint_num; i++)
    {
      nh.param("/single_offboard/waypoint" + to_string(i) + "_x", waypoint[i][0], -1.0);
      nh.param("/single_offboard/waypoint" + to_string(i) + "_y", waypoint[i][1], -1.0);
      nh.param("/single_offboard/waypoint" + to_string(i) + "_z", waypoint[i][2], -1.0);
    }
//    nh.param("/single_offboard/waypoint1_x",aim.pose.position.x,1.0);
//    nh.param("/single_offboard/waypoint1_y",aim.pose.position.y,2.0);
//    nh.param("/single_offboard/waypoint1_z",aim.pose.position.z,3.0);
    new std::thread(&UdpListen,12001);
    // wait for FCU connection
    while(ros::ok() && !current_state.connected){
        ros::spinOnce();
        rate.sleep();
    }
    sensor_msgs::BatteryState voltage_now;
    
    mavros_msgs::SetMode offb_set_mode;
    offb_set_mode.request.custom_mode="OFFBOARD";

    mavros_msgs::SetMode land_mode;
    land_mode.request.custom_mode = "LAND";

    mavros_msgs::SetMode mode_cmd;

    mavros_msgs::CommandBool arm_cmd,disarm_cmd;
    arm_cmd.request.value = true;
    disarm_cmd.request.value = false;

    ros::Time last_request = ros::Time::now();
    for(int i = 100; ros::ok() && i > 0; --i)
    {
        local_pos_pub.publish(aim);
        ros::spinOnce();
        rate.sleep();
    }




    while(ros::ok())
    {
        //waypoint
        if(cmdd==6)
        {   
//          if(fabs(pos_drone_fcu[0] - waypoint[j][0]) < (0.05) && fabs(pos_drone_fcu[1] - waypoint[j][1]) < (0.05) && fabs(pos_drone_fcu[2] - waypoint[j][2]) < (0.05))
          
        if(sqrt(pow(pos_drone_fcu[0] - waypoint[j][0],2)+pow(pos_drone_fcu[1] - waypoint[j][1],2)+pow(pos_drone_fcu[2] - waypoint[j][2],2))<(0.1) && j<waypoint_num)
          j=j+1;
          aim.pose.position.x = waypoint[j][0];
          aim.pose.position.y = waypoint[j][1];
          aim.pose.position.z = waypoint[j][2];
        }
        //fsm
//        if(cmdd!=4) local_pos_pub.publish(aim);
        if(cmdd==1)
        {
            if( current_state.mode != "OFFBOARD" &&
                (ros::Time::now() - last_request > ros::Duration(5.0))){
                if( set_mode_client.call(offb_set_mode) &&
                    offb_set_mode.response.mode_sent){
                    ROS_ERROR("Offboard enabled");
                }
                last_request = ros::Time::now();
            }
        }
        if(cmdd==2)
        {
            if( current_state.mode != "OFFBOARD" &&
                (ros::Time::now() - last_request > ros::Duration(3.0))){
                if( set_mode_client.call(offb_set_mode) &&
                    offb_set_mode.response.mode_sent){
                    ROS_ERROR("Offboard enabled");
                }
                last_request = ros::Time::now();
            }else if( !current_state.armed &&
                (ros::Time::now() - last_request > ros::Duration(3.0))){
                if( arming_client.call(arm_cmd) &&
                    arm_cmd.response.success){
                    ROS_ERROR("Vehicle armed");
                }
                last_request = ros::Time::now();
            }
            local_pos_pub.publish(aim);
        }
        
        if(cmdd==9)
        {
            if( current_state.mode != "OFFBOARD" &&
                (ros::Time::now() - last_request > ros::Duration(5.0)))
            {
                if( set_mode_client.call(offb_set_mode) &&
                    offb_set_mode.response.mode_sent)
                {
                  ROS_ERROR("Offboard enabled");
                }
                last_request = ros::Time::now();
            }
            else if( !current_state.armed &&
                (ros::Time::now() - last_request > ros::Duration(5.0)))
            {
               if( arming_client.call(arm_cmd) &&
                    arm_cmd.response.success)
                {
                    ROS_ERROR("Vehicle armed");
                }
                last_request = ros::Time::now();
            }
                  ROS_ERROR("Vehicle armed");
        }
        
        if(cmdd == 4)
        {   
            if(abs(pos_drone_fcu[2] - 0.1) < 0.05) {flag1 = 1;}
            if(flag1 == 0)
            {
                aim.pose.position.x = pos_drone_fcu[0];
                aim.pose.position.y = pos_drone_fcu[1];      
                aim.pose.position.z = 0.1;
                local_pos_pub.publish(aim);
            }
            else
            {
                if(current_state.mode == "OFFBOARD")
                {
                    mode_cmd.request.custom_mode = "Hold";
                    set_mode_client.call(mode_cmd);
                    ROS_ERROR("1111");
                }
                if(current_state.armed)
                {
                    arm_cmd.request.value = false;
                    arming_client.call(arm_cmd);
                    ROS_ERROR("2222");
                }
                if (arm_cmd.response.success)
                {
                    ROS_ERROR("Disarm successfully!");
                }
            }
        }

        if(cmdd==5)
        {
            if(current_state.mode == "OFFBOARD")
            {
                mode_cmd.request.custom_mode = "MANUAL";
                set_mode_client.call(mode_cmd);
            }

            if(current_state.armed)
            {
                arm_cmd.request.value = false;
                arming_client.call(arm_cmd);
            }

            if (arm_cmd.response.success)
            {
                ROS_ERROR("Disarm successfully!");
            }
        }

        /*----------ASCUP----------*/
        if(cmdd == 7)
        {
            /*----------mission A----------*/
            if(flag_A == 1)
            {
                /*----------point 1: search for bounding box----------*/
                if(flag_a == 0)
                {
                    /*----------avoid recognition noise: 10 correct recognition as successful----------*/
                    if(count_box < 10)
                    {                        
                        /*----------fly towards right slowly (maximum y = -4.5) before successful recognition */
                        if(pos_drone_fcu[1] > -4.5)
                        {
                            aim.pose.position.x = 0.0;
                            aim.pose.position.y = pos_drone_fcu[1] - 0.02;      
                            aim.pose.position.z = 0.6;
                            local_pos_pub.publish(aim);
                        }
                        else
                        {
                            aim.pose.position.x = 0.0;
                            aim.pose.position.y = -4.5;      
                            aim.pose.position.z = 0.6;
                            local_pos_pub.publish(aim);
                        }
                    }
                    else
                    {
                        flag_a = 1;
                    }  
                }
                
                /*----------point 2: bounding box right midpoint----------*/
                if(flag_a == 1)
                {
                    /*----------check position arrival: box right midpoint----------*/
                    if( (abs(pos_drone_fcu[0] - box_midpoint[0]) < 0.1) && (abs(pos_drone_fcu[1] - box_midpoint[1]) < 0.1) )
                    {
                        /*----------check yaw arrival: 90 deg----------*/
                        if( (abs(yaw_drone_fcu[0] - 0.707) < 0.004) && (abs(yaw_drone_fcu[1] - 0.707) < 0.004) )
                        {
                            flag_a = 2;
                        }
                        /*----------adjust yaw----------*/
                        else
                        {
                            aim.pose.position.x = box_midpoint[0];
                            aim.pose.position.y = box_midpoint[1];      
                            aim.pose.position.z = box_midpoint[2];
                            aim.pose.orientation.x = 0.7071;
                            aim.pose.orientation.y = 0.0;
                            aim.pose.orientation.z = 0.0;
                            aim.pose.orientation.x = 0.7071;
                            local_pos_pub.publish(aim);
                        }
                    }
                    /*----------fly towards box right midpoint through planning----------*/
                    else
                    {
                        //_____planning function()    start point: pos_drone_fcu[0], pos_drone_fcu[1], 0.6    end point: right midpoint of bounding box
                    }

                }

                /*----------point 3: go through wall----------*/
                if(flag_a == 2)
                {
                    /*----------check position arrival: box left midpoint----------*/
                    if( (abs(pos_drone_fcu[0] - box_midpoint[0]) < 0.1) && (abs( pos_drone_fcu[1] - (box_midpoint[1] + 7.0) ) < 0.1) )
                    {
                        /*----------check yaw arrival: 0 deg----------*/
                        if( (abs(yaw_drone_fcu[0] - 1.0) < 0.001) && (abs(yaw_drone_fcu[1] - 0.0) < 0.005) )      
                        {
                            flag_a = 3;
                        }
                        /*----------adjust yaw----------*/
                        else
                        {
                            aim.pose.position.x = box_midpoint[0];
                            aim.pose.position.y = box_midpoint[1] + 7.0;      
                            aim.pose.position.z = box_midpoint[2];
                            aim.pose.orientation.x = 1.0;
                            aim.pose.orientation.y = 0.0;
                            aim.pose.orientation.z = 0.0;
                            aim.pose.orientation.x = 0.0;
                            local_pos_pub.publish(aim);
                        }
                    }
                    /*----------fly towards box right midpoint through planning----------*/
                    else
                    {
                        //_____planning function()    start point: right midpoint of bounding box    end point: right midpoint of bounding box with y + 7.0
                    }
                }

                /*----------point 4: transition to mission B----------*/
                if(flag_a == 3)
                {
                    aim.pose.position.x = box_midpoint[0];
                    aim.pose.position.y = box_midpoint[1] +7.0;      
                    aim.pose.position.z = 0.1;
                    local_pos_pub.publish(aim);
                }
            }

            
            
            /*----------mission D----------*/
            if(flag_D == 1)
            {    
                if(flag_ring1 == 0)
                {
                    /*----------check ring detection----------*/
                    if(count_ring1 < 10)
                    {
                        if(flag_ring_recog == 1)
                        {
                            count_ring1 = count_ring1 + 1;
                            flag_ring_recog = 0;
                        }
                        aim.pose.position.x = pos_drone_fcu[0];
                        aim.pose.position.y = pos_drone_fcu[1];      
                        aim.pose.position.z = pos_drone_fcu[2];
                        local_pos_pub.publish(aim);
                    }
                    /*----------determine point before and after ring----------*/
                    else if(count_ring1 == 10)
                    {
                        count_ring1 = count_ring1 + 1;

                        ring_before = Eigen::Vector3d(pos_drone_fcu[0] + ring_center[0] - ring_direct[0]*1.0, 
                                                    pos_drone_fcu[1] + ring_center[1] - ring_direct[1]*1.0, 
                                                    pos_drone_fcu[2] + ring_center[2]);
                        ring_after = Eigen::Vector3d(pos_drone_fcu[0] + ring_center[0] + ring_direct[0]*1.0, 
                                                    pos_drone_fcu[1] + ring_center[1] + ring_direct[1]*1.0, 
                                                    pos_drone_fcu[2] + ring_center[2]);                         
                        aim.pose.position.x = ring_before[0];
                        aim.pose.position.y = ring_before[1];      
                        aim.pose.position.z = ring_before[2];
                        local_pos_pub.publish(aim);
                    }
                    /*----------fly towards point before ring----------*/
                    else if(count_ring1 == 11)
                    {
                        aim.pose.position.x = ring_before[0];
                        aim.pose.position.y = ring_before[1];      
                        aim.pose.position.z = ring_before[2];
                        local_pos_pub.publish(aim);

                        if( (abs(pos_drone_fcu[0] - ring_before[0]) < 0.1) && (abs(pos_drone_fcu[1] - ring_before[1]) < 0.1) && (abs(pos_drone_fcu[2] - ring_before[2]) < 0.1) )
                        {
                            count_ring1 = count_ring1 + 1;
                        }
                    }
                    /*----------fly towards point after ring----------*/
                    else
                    {
                        aim.pose.position.x = ring_after[0];
                        aim.pose.position.y = ring_after[1];      
                        aim.pose.position.z = ring_after[2];
                        local_pos_pub.publish(aim);

                        if( (abs(pos_drone_fcu[0] - ring_after[0]) < 0.1) && (abs(pos_drone_fcu[1] - ring_after[1]) < 0.1) && (abs(pos_drone_fcu[2] - ring_after[2]) < 0.1) )
                        {
                            flag_ring1 = 1;
                        }
                    }
                }

                if(flag_ring1 == 1)
                {
                    aim.pose.position.x = pos_drone_fcu[0];
                    aim.pose.position.y = pos_drone_fcu[1];      
                    aim.pose.position.z = 0.1;
                    local_pos_pub.publish(aim);
                }
            }



            /*----------mission LAND----------*/
            if(flag_LAND == 1)
            {
                /*----------determine wait coordinate----------*/
                if(flag_land == 0)
                {
                    pos_drone_now = Eigen::Vector3d(pos_drone_fcu[0], pos_drone_fcu[1], pos_drone_fcu[2]);
                    aim.pose.position.x = pos_drone_now[0];
                    aim.pose.position.y = pos_drone_now[1];      
                    aim.pose.position.z = pos_drone_now[2];
                    local_pos_pub.publish(aim);
                    
                    flag_land = 1;
                }
                
                /*----------wait and check tag recognition: 10 recognition as successful----------*/
                if(flag_land == 1)
                {
                    aim.pose.position.x = pos_drone_now[0];
                    aim.pose.position.y = pos_drone_now[1];      
                    aim.pose.position.z = pos_drone_now[2];
                    local_pos_pub.publish(aim);

                    if(flag_tag_recog == 1)
                    {
                        count_tag = count_tag + 1;
                        flag_tag_recog = 0;
                    }
                    if(count_tag == 10) {flag_land = 2;}
                }

                /*----------determine tag coordinate----------*/
                if(flag_land == 2)
                {
                    pos_land = Eigen::Vector3d(pos_drone_now[0] + pos_tag[0], pos_drone_now[1] + pos_tag[1], 0.1);
                    aim.pose.position.x = pos_land[0];
                    aim.pose.position.y = pos_land[1];      
                    aim.pose.position.z = pos_land[2];
                    local_pos_pub.publish(aim);

                    flag_land = 3;
                }

                /*----------fly towards tag----------*/
                if(flag_land == 3)
                {
                    aim.pose.position.x = pos_land[0];
                    aim.pose.position.y = pos_land[1];      
                    aim.pose.position.z = pos_land[2];
                    local_pos_pub.publish(aim);

                    if( (abs(pos_drone_fcu[0] - pos_land[0]) < 0.1) 
                     && (abs(pos_drone_fcu[1] - pos_land[1]) < 0.1) 
                     && (abs(pos_drone_fcu[2] - 0.1) < 0.05) )      {flag_land = 4;}
                }
                
                /*----------land and lock----------*/
                if(flag_land == 4)
                {
                    if(current_state.mode == "OFFBOARD")
                    {
                        mode_cmd.request.custom_mode = "Hold";
                        set_mode_client.call(mode_cmd);
                        ROS_ERROR("1111");
                    }
                    if(current_state.armed)
                    {
                        arm_cmd.request.value = false;
                        arming_client.call(arm_cmd);
                        ROS_ERROR("2222");
                    }
                    if (arm_cmd.response.success)
                    {
                        ROS_ERROR("Disarm successfully!");
                    }
                }
            }





        }


        if(cmdd == 8)
        {
            
        //    if(flag_new_traj == 1)
        //    {
        //        traj_p = 0;
        //        flag_new_traj = 0;
        //    }
            // goal.pose.position.x = 1.0;
            // goal.pose.position.y = -5.0;
            // goal.pose.position.z = 0.3;
            // //    cout<<"fuck"<<endl;
            // traj_goal_pub.publish(goal);
            ros::Time time_1 = ros::Time::now();    
            // cout<<"flag_new_traj="<<flag_new_traj<<endl;
            // if(flag_new_traj == 0)
            // {
                if(jam == 1) 
                {
                    local_pos_pub.publish(aim);
                }
                else{
                if((traj.end()-1) != traj.begin())
                {
                    Eigen::Vector2d ptr;
                    ptr = *(traj.end()-1);
                    traj.pop_back();
                    // double x = goal.pose.position.x-pos_drone_fcu[0];
                    // double y = goal.pose.position.y-pos_drone_fcu[1];
                    double x = ptr(0)-pos_drone_fcu[0];
                    double y = ptr(1)-pos_drone_fcu[1];
                    // double x = traj_fsm.position[traj_p].pose.position.x-aim.pose.position.x;
                    // double y = traj_fsm.position[traj_p].pose.position.y-aim.pose.position.y;
                    // double x = traj_fsm.velocity[traj_p].pose.position.x;
                    // double y = traj_fsm.velocity[traj_p].pose.position.y;
                    complex<double> cm(x,y);  
                    double arg_ = arg(cm);
                    geometry_msgs::Quaternion geo_q = tf::createQuaternionMsgFromYaw(arg_);
                    aim.pose.position.x = traj_fsm.position[traj_p].pose.position.x;
                    aim.pose.position.y = traj_fsm.position[traj_p].pose.position.y;
                    aim.pose.position.z = 0.3;
                    aim.pose.orientation = geo_q;
                    
                    
                    local_pos_pub.publish(aim);
                    // rate.sleep();
                    traj_p = traj_p - 1;
                }
                else
                {
                    local_pos_pub.publish(aim);
                }
                }
                cout<<aim.pose.position.x<<"    "<<aim.pose.position.y<<"    "<<aim.pose.position.z<<endl;
                
            // }    

        }



        ros::spinOnce();
        rate.sleep();
    }
    return 0;

}



/* yaw:  0 deg      1      0      0      0
        90 deg    0.707    0      0    0.707
       180 deg      0      0      0      1
       -90 deg    0.707    0      0   -0.707    */
