#include "fsm_func.h"

namespace fsm_func
{
     /*-----------------INITIALIZE-----------------*/
    drone_controller::drone_controller(){}
    drone_controller::drone_controller(ros::NodeHandle & nh)
    {
        node = nh;
        ros_feq = 20.0;  
        // param
        takeoff_height = 0.8;
        aim_for_offboard.pose.position.x = 0.0;
        aim_for_offboard.pose.position.y = 0.0;
        aim_for_offboard.pose.position.z = 0.0;
        aim_for_offboard.pose.orientation.w = 1.0;
        aim_for_offboard.pose.orientation.x = 0.0;
        aim_for_offboard.pose.orientation.y = 0.0;
        aim_for_offboard.pose.orientation.z = 0.0;

        _aim_ = aim_for_offboard;
        _aim_.pose.position.z = takeoff_height;
        //  nh.param("planning/traj_order", p_order_, 3);

        // sub & pub & srv & client
        joy_sub = nh.subscribe("/sim_joy/joy",10,&drone_controller::joy_cb,this);
        state_sub = nh.subscribe("/mavros/state", 10, &drone_controller::state_cb,this);
        position_sub = nh.subscribe("/mavros/local_position/pose", 100, &drone_controller::pos_cb,this);
        voltage_sub = nh.subscribe("/mavros/battery", 300, &drone_controller::vtg_cb,this);
        bspline_sub = nh.subscribe("/bspline_traj",1,&drone_controller::traj_cb,this);
        arrived_sub = nh.subscribe("/astar_node/target_arrived",1,&drone_controller::arrived_cb,this);
        arming_client = nh.serviceClient<mavros_msgs::CommandBool>("mavros/cmd/arming");
        set_mode_client = nh.serviceClient<mavros_msgs::SetMode>("mavros/set_mode");
        call_bs_client = nh.serviceClient<std_srvs::Trigger>("/call_bs");
        local_pos_pub = nh.advertise<geometry_msgs::PoseStamped>("mavros/setpoint_position/local", 10);
    }
     /*-----------------CALL_BACK-----------------*/
    void drone_controller::joy_cb(const std_msgs::Int32::ConstPtr & msg)
    {
        cmd_code = Cmd_State(msg->data);
    }
    void drone_controller::state_cb(const mavros_msgs::State::ConstPtr & msg)
    {
        current_state = *msg;
        cout<<"[PX4] Current state: "<<current_state.mode<<endl;
    }
    void drone_controller::pos_cb(const geometry_msgs::PoseStamped::ConstPtr &msg)
    {
        pos_drone_fcu_lst = pos_drone_fcu;
        pos_drone_fcu  = Eigen::Vector3d(msg->pose.position.x,msg->pose.position.y,msg->pose.position.z);
        // cout<<"----------------pose_cb---------------"<<"\n";
        // cout<<pos_drone_fcu<<endl;
    }
    void drone_controller::vtg_cb(const sensor_msgs::BatteryState::ConstPtr &msg)
    {
        voltage_now = msg->voltage;
    }
    void drone_controller::traj_cb(const bspline_race::BsplineTraj::ConstPtr & msg)
    {
        // if(!traj_saved){
        //  for(int i = 0;i<msg->position.size();i++)
        // {
        //     double error;
        //     error = sqrt(pow((msg->position[i].pose.position.x-pos_drone_fcu[0]),2)+pow((msg->position[i].pose.position.y-pos_drone_fcu[1]),2));
        //     if(error<=0.05)
        //     {
        //         cout<<"final pos: "<<msg->position[i].pose.position.x<<" , "<<msg->position[i].pose.position.y<<endl;
        //         cout<<"near:" <<error<<endl;
        //         break;
        //     }
        //     else{
        //         Eigen::Vector2d ptr;
        //         ptr << msg->position[i].pose.position.x,msg->position[i].pose.position.y;
        //         // cout<<"get i = "<<i<<"with error: "<<error<<endl;
        //         traj.push_back(ptr);
        //     }
        // }
        // }
        // traj_saved = true;
        if(bs_replanned)
        {
        double error_scale = sqrt(pow((msg->position[msg->position.size()-1].pose.position.x-pos_drone_fcu[0]),2)
                                                           +pow((msg->position[msg->position.size()-1].pose.position.y-pos_drone_fcu[1]),2))
                                                           +0.1;
        cout<<"scallllllllllllllllllllllllle:"<<error_scale<<endl;
        for(int i = msg->position.size()-1;i>=0;i--)
        {
            double error;
            error = sqrt(pow((msg->position[i].pose.position.x-pos_drone_fcu[0]),2)+pow((msg->position[i].pose.position.y-pos_drone_fcu[1]),2));
            if(error>1.5)
            {
                cout<<"final pos: "<<msg->position[i].pose.position.x<<" , "<<msg->position[i].pose.position.y<<endl;
                cout<<"near:" <<error<<endl;
                break;
            }
            else if(error<=error_scale)
            {
                cout<<"Exclude start."<<endl;
            }
            else
            {
                Eigen::Vector2d ptr;
                ptr << msg->position[i].pose.position.x,msg->position[i].pose.position.y;
                traj.push_back(ptr);
            }
        }
        bs_replanned = false;
        }

    }
    void drone_controller::arrived_cb(const std_msgs::Int32::ConstPtr & msg)
    {
        if(msg->data>0)
            arrived = true;
        else arrived = false;
        cout<<"[Astar] arrived?"<<arrived<<arrived<<arrived<<arrived<<arrived<<endl;
    }
     /*-----------------RUN(ROS_SPIN)-----------------*/
    void drone_controller::run()
    {
        if(wait_connect()) cout<<"\n[FSM] Connection Established!"<<endl;
        else 
        {
            ROS_ERROR("[FSM] Cannot connect to drone! Shutdown.");
            return;
        }
        last_request = ros::Time::now();
        pre_send();
        ros::Timer timer = node.createTimer(ros::Duration(1.0/ros_feq), &drone_controller::fsm, this);
        ros::spin();
    }
     /*-----------------FSM-----------------*/
    void drone_controller::fsm(const ros::TimerEvent& e)
    {
        switch (cmd_code)
        {
            case Hold:
            {
                local_pos_pub.publish(_aim_);
                break;
            }
             case Disarm:
            {
                bool finish_flag = 0;
                while(!finish_flag) finish_flag = hold();
                finish_flag = 0;
                while(!finish_flag) finish_flag = disarm();
                cmd_code = Idle;
                break;
            }
             case Takeoff:
            {
                if(!aim_saved) 
                {   
                    land_saved = false;
                    _aim_.pose.position.x = pos_drone_fcu(0);
                    _aim_.pose.position.y = pos_drone_fcu(1);
                    _aim_.pose.position.z = takeoff_height;
                    complex<double> cm(0.0,-1);  
                    double arg_ = arg(cm);
                    geometry_msgs::Quaternion geo_q = tf::createQuaternionMsgFromYaw(arg_);
                    _aim_.pose.orientation = geo_q;
                    aim_saved = true;
                }
                local_pos_pub.publish(_aim_);
                takeoff();
                break;
            }
             case Land:
            {
                if(!land_saved) 
                {   
                    aim_saved = false;
                    _aim_.pose.position.x = pos_drone_fcu(0);
                    _aim_.pose.position.y = pos_drone_fcu(1);
                    _aim_.pose.position.z = 0.05;
                    land_saved = true;
                }
                if(abs(pos_drone_fcu[2]-0.1)>0.05) local_pos_pub.publish(_aim_);
                else cmd_code = Disarm;
                break;
            }
             case Automatic:
            {
                automatic();
                break;
            }
                
            case Idle:
                /* code */
                break;
            
            default:
                break;
        }
    }
    /*-----------------FSM_FUNC-----------------*/
    bool drone_controller::offboard()
    {
        mavros_msgs::SetMode mode;
        mode.request.custom_mode="OFFBOARD";
        // if(current_state.armed == true) return true;
        return set_mode(mode,3.0);
    }
    bool drone_controller::arm()
    {
        mavros_msgs::CommandBool cmd;
        cmd.request.value = true;
        return set_command(cmd,3.0);
    }
    bool drone_controller::disarm()
    {
        mavros_msgs::CommandBool cmd;
        cmd.request.value = false;
        return set_command(cmd,3.0);
    }
    bool drone_controller::hold()
    {
        mavros_msgs::SetMode mode;
        mode.request.custom_mode="AUTO.LAND";
        return set_mode(mode,3.0);
    }

    void drone_controller::takeoff()
    {
        if(!offboard())
        {
            if(!arm())
            {
                cmd_code = Hold;
            }
        } 
        if(current_state.mode!="OFFBOARD") offboard();
        bool finish_flag = 0;
        if(!finish_flag) finish_flag = offboard();
        finish_flag = 0;
        while(!finish_flag) finish_flag = arm();
    }

    void drone_controller::automatic()
    {
        ros::Rate rate(20.0);
        double error = sqrt(pow((pos_drone_fcu(0)-_aim_.pose.position.x),2)+pow((pos_drone_fcu(1)-_aim_.pose.position.y),2));
         if(traj.size() != 0)
        {
            Eigen::Vector2d ptr;
            // ptr = *(traj.end()-1);
            // traj.pop_back();
            ptr = *(traj.begin());
            traj.erase(traj.begin());
            double rl,im;
            geometry_msgs::PoseStamped pos_ptr;
            // pos
            pos_ptr.pose.position.x = ptr(0);
            pos_ptr.pose.position.y = ptr(1);
            pos_ptr.pose.position.z = 0.8;
            _aim_ = pos_ptr;
            // ori
            Eigen::Vector2d ptr_ = *(traj.begin()+1);
            // ver3
            rl = pos_drone_fcu(0) - pos_drone_fcu_lst(0);
            im = pos_drone_fcu(1) - pos_drone_fcu_lst(1);
            // ver1
            // rl = ptr(0) - pos_drone_fcu(0);
            // im = ptr(1) - pos_drone_fcu(1);
            // ver2
            // if(traj.size()==1){
            // rl = ptr(0) - pos_drone_fcu(0);
            // im = ptr(1) - pos_drone_fcu(1);
            // }
            // else{
            // rl = ptr(0) - ptr_(0);
            // im = ptr(1) - ptr_(1);
            // }
            complex<double> cm(rl,im);  
            double arg_ = arg(cm);
            if(arg_<0) arg_ = arg_ + 2*PI;
            // cout<<"----------ori test-------------"<<endl;
            // cout<<"arg for now:"<<arg_<<endl;
            // cout<<"cm for now:"<<cm<<endl;

            geometry_msgs::Quaternion geo_q = tf::createQuaternionMsgFromYaw(arg_);
            _aim_.pose.orientation = geo_q;
        }
        else
        {
            std_srvs::Trigger srv;
            if (call_bs_client.call(srv)) 
            {
                bs_replanned = srv.response.success;
                string message = srv.response.message;
                // reverse(message.begin(),message.end());
                ROS_INFO("%s",message.c_str());
            }
        }
        rate.sleep();
        local_pos_pub.publish(_aim_);
    }
 


    bool drone_controller::set_mode(mavros_msgs::SetMode mode,double durantion)
    {
        bool return_flag = false;
        if( current_state.mode != mode.request.custom_mode &&
            (ros::Time::now() - last_request > ros::Duration(durantion))){
                cout<<"Time past: "<<ros::Time::now() - last_request<<endl;
            if( set_mode_client.call(mode) &&
                mode.response.mode_sent){
                cout<<"[FSM] Mode "<<mode.request.custom_mode<< " enabled."<<endl;
                return_flag = true;
            }
            last_request = ros::Time::now();
        }
        return return_flag;
    }
    bool drone_controller::set_command( mavros_msgs::CommandBool cmd,double durantion )
    {
        bool return_flag = false;
        if( current_state.armed != cmd.request.value &&
            (ros::Time::now() - last_request > ros::Duration(durantion))){
                cout<<"Time past: "<<ros::Time::now() - last_request<<endl;
                if( arming_client.call(cmd) &&
                    cmd.response.success){
                        string str_;
                        if(cmd.request.value) str_ = "";
                        else str_ = "dis";
                        cout<<"[FSM] Vehicle "<<str_<< "armed."<<endl;
                        return_flag = true;
                }
                last_request = ros::Time::now();
            }
            return return_flag;
    }

    /*-----------------CONNECTION_ESTABLISH-----------------*/
    bool drone_controller::wait_connect()
    {
        int total_connect_time = 0;
        ros::Rate rate(ros_feq);
        cout<<"[fsm] Wait connection..";
        while(ros::ok() && !current_state.connected&&total_connect_time++<ros_feq*10)
        {
            ros::spinOnce();
            rate.sleep();
            cout<<"  .  ";
        }
        if(current_state.connected) return true;
        else return false;
    }

    void drone_controller::pre_send()
    {
        ros::Rate rate(ros_feq);
        geometry_msgs::PoseStamped aim_for_offboard;
        for(int i = 100; ros::ok() && i > 0; --i){
            local_pos_pub.publish(aim_for_offboard);
            ros::spinOnce();
            rate.sleep();
        }
    }
}