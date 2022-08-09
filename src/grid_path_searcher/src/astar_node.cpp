/*
输入：
输出：
*/
#include <iostream>
#include <fstream>
#include <math.h>
#include <pcl_conversions/pcl_conversions.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <ros/ros.h>
#include <ros/console.h>
#include <sensor_msgs/PointCloud2.h>
#include <sensor_msgs/PointCloud.h>
#include <nav_msgs/Odometry.h>
#include <nav_msgs/Path.h>
#include <geometry_msgs/PoseStamped.h>
#include <geometry_msgs/PointStamped.h>
#include <visualization_msgs/MarkerArray.h>
#include <visualization_msgs/Marker.h>
#include <std_msgs/Int32.h>
#include <std_msgs/Bool.h>
#include <std_srvs/Trigger.h>
#include "Astar_searcher_2d.h"
// #include "JPS_searcher.h"
//#include "backward.hpp"
#include <sensor_msgs/point_cloud_conversion.h>


using namespace std;
using namespace Eigen;

// namespace backward {
// backward::SignalHandling sh;
// }
/*----------------------global variables----------------------*/
// Simulation param from launch file
double _resolution, _inv_resolution, _cloud_margin;
double _x_size, _y_size;    

bool _has_map   = false;
bool _has_target   = false;
bool plan_required = false;
bool arrived_part_target = false;
Vector2d _start_pt;
vector<Vector2d> _target_pt;
vector<Vector2d> grid_path_global;
vector<Vector2d> grid_twist_global;
double actual_height = 0.0;
// Map size
Vector2d _map_lower, _map_upper;
int _max_x_id, _max_y_id;
/*----------------------ROS----------------------*/
// SubPub
ros::Subscriber _map_sub,
                                 _pts_sub,
                                 _nav_sub,
                                 _plan_sub;//读地图与目标点
ros::Publisher  _grid_path_vis_pub, 
                               _visited_nodes_vis_pub,
                               _grid_map_vis_pub,
                               _grid_path_pub,
                               _grid_twist_pub,
                               _arrived_pub;

// Callback
void rcvWaypointsCallback(const nav_msgs::Path & wp_);
void rcvPointCloudCallBack(const sensor_msgs::PointCloud2::ConstPtr & pointcloud_map_raw);
void simPoseCallback(const geometry_msgs::PoseStamped & msg);
/*----------------------A*----------------------*/
AstarPathFinder2d * _astar_path_finder     = new AstarPathFinder2d();
bool pathFinding(const Vector2d start_pt, const Vector2d target_pt);
/*----------------------visual----------------------*/
void visGridPath( vector<Vector2d> nodes, bool is_use_jps );                                            //可视化函数
void visVisitedNode( vector<Vector2d> nodes );           
/*----------------------message pub----------------------*/
void pubGridPath(vector<Vector2d> nodes);
void pubGridTwist(vector<Vector2d> nodes);

/*----------------------function----------------------*/
void rcvPointCloudCallBack(const sensor_msgs::PointCloud2::ConstPtr & pointcloud_map_raw)
{   
    sensor_msgs::PointCloud2 pointcloud_map = *pointcloud_map_raw;
    // convertPointCloudToPointCloud2(*pointcloud_map_raw, pointcloud_map);
    if(_has_map){
        //QUES 是否需要zhiling？
        _astar_path_finder->cleanObs();
         cout<<"Has map, clean all."<<endl;
    }
     
    pcl::PointCloud<pcl::PointXYZ> cloud;           //容器填充为三维点
    pcl::PointCloud<pcl::PointXYZ> cloud_vis;
    sensor_msgs::PointCloud2 map_vis;
     
    pcl::fromROSMsg(pointcloud_map, cloud);//类型变换
     
    if( (int)cloud.points.size() == 0 ) return;//空地图则返回
     
    pcl::PointXYZ pt;
    for (int idx = 0; idx < (int)cloud.points.size(); idx++)//遍历点云中的点
    {
        //FIXME 输入点云是三维的，这里需要确定z的范围
        //QUES 是不是0上下的一个范围？
        // if(高度不在范围内) continue;
        pt = cloud.points[idx];        //取点到pt
        //FIXME 修改0.5为目标高度范围
        if(std::abs(pt.z-0.7)>_resolution) continue;
         _astar_path_finder->setObs(pt.x, pt.y);
        // for visualize only
        // 可视化
        Vector2d cor_round = _astar_path_finder->coordRounding(Vector2d(pt.x, pt.y));
        pt.x = cor_round(0);
        pt.y = cor_round(1);
        pt.z = 0;
        cloud_vis.points.push_back(pt);
        Vector2i center_index = _astar_path_finder -> coord2gridIndex(Vector2d(pt.x, pt.y));
        Vector2d pt_around_d;
        Vector2i  pt_around_i;
        for(int row_index = -1;row_index<=1;row_index++){
            for(int col_index = -1;col_index<=1;col_index++){
                if(row_index == 0 && col_index == 0) continue;
                pt_around_i = center_index + Vector2i(row_index,col_index);
                pt_around_d = _astar_path_finder -> gridIndex2coord(pt_around_i);
                if(_astar_path_finder -> getData(pt_around_i)) {
                    pt.x = pt_around_d(0);
                    pt.y = pt_around_d(1);
                    cloud_vis.points.push_back(pt);
                }
            }
        }
        
        
        // 可视化end
    }
     
    // 可视化
    cloud_vis.width    = cloud_vis.points.size();
    cloud_vis.height   = 1;
    cloud_vis.is_dense = true;

    pcl::toROSMsg(cloud_vis, map_vis);  //将处理好的可视化数据发布到ROS话题中
    map_vis.header.frame_id = "t265_odom_frame";
    _grid_map_vis_pub.publish(map_vis);//demo_node/grid_map_vis
    // 可视化end

    _has_map = true;
    cout<<"[Astar] Pointcloud received."<<endl;
    std_msgs::Int32 arr_msg;
  
    bool early_stop = false;
    Vector2d _target_pt_;
    Vector2d _start_pt_;
    _start_pt_ = _start_pt;
    cout<<"size: "<<_target_pt.size()<<endl;
    // for(int i = 0;i<_target_pt.size();i++)
    for(auto iter = _target_pt.begin();iter!=_target_pt.end();iter++)
    {
        _target_pt_ = *(iter);
        cout<<_target_pt_<<endl;
        if(!_has_target || !plan_required)
        {
            ROS_WARN_DELAYED_THROTTLE(1,"[Astar] No target!");
            early_stop = true;
            break;
        }
        else  {
            if(_astar_path_finder -> getData(_target_pt_)) 
            {
                ROS_ERROR_DELAYED_THROTTLE(2,"[Astar] _target_pt is Occupied! PathFinding will not run!");
                early_stop = true;
                break;
            }
            else if(_astar_path_finder->arrived(_start_pt,_target_pt[_target_pt.size()-1])) 
            {
                ROS_WARN_DELAYED_THROTTLE(1,"[Astar] Arrived! PathFinding will not run!");
                arr_msg.data = 1;
                _arrived_pub.publish(arr_msg);
                early_stop = true;
                break;
            }
            else if(arrived_part_target)
            {
                cout<<"_start_pt"<<_start_pt<<endl;
                cout<<"_target_pt_"<<_target_pt_<<endl;
                continue;
            }
            else {
                if(_astar_path_finder->getData(_start_pt_)) 
                {
                    ROS_WARN_DELAYED_THROTTLE(1,"[Astar] _start_pt is Occupied! Reset Obs. BE CAREFUL!");
                    _astar_path_finder -> cleanStartObs(_start_pt_);
                }
                if(!pathFinding(_start_pt_, _target_pt_)) 
                {
                    ROS_ERROR_DELAYED_THROTTLE(2,"[Astar] No path provide!"); 
                    early_stop = true;
                    break;
                }
                else{
                    arr_msg.data = -1;
                    _arrived_pub.publish(arr_msg);
                }
            }
        }
        _start_pt_ = _target_pt_;
    }

    if(!early_stop && grid_twist_global.size()!=0 && grid_path_global.size()!=0)
    {
        //Visualize the result
        grid_path_global.pop_back();
        grid_path_global.pop_back();
        grid_path_global.pop_back();
        grid_path_global.pop_back();
        grid_twist_global.pop_back();
        grid_twist_global.pop_back();
        grid_twist_global.pop_back();
        grid_twist_global.pop_back();
        visGridPath (grid_path_global, false);
        // visVisitedNode(visited_nodes);
        pubGridPath(grid_path_global);
        pubGridTwist(grid_twist_global);
    }

    if(arrived_part_target)
    {
        _target_pt.erase(_target_pt.begin());
    }
    arrived_part_target = false;
    grid_path_global.clear();
    grid_twist_global.clear();

}

void rcvWaypointsCallback(const nav_msgs::Path::ConstPtr & wp_)
{    
    _target_pt.clear();
    //USAGE 拿到waypoint消息后，检查并进行路径寻找
    //起点为全局变量_start_pt，终点为消息中的waypoint
    if( _has_map == false ) //无地图，退出函数
    {
        ROS_WARN("[Astar] No map! ");
        // return;
    }
    cout<<"[Astar] Planning target received."<<endl;
    nav_msgs::Path wp__ = *(wp_);
    Vector2d _target_pt_;
    for(auto wp = wp__.poses.begin();wp!=wp__.poses.end();wp++)
    {
        _target_pt_ << wp->pose.position.x,
                                wp->pose.position.y;
        _target_pt.push_back(_target_pt_);
    }
    // reverse(_target_pt.begin(),_target_pt.end());
    _has_target = true;
    // Vector2d _target_pt;

    // ROS_INFO("[node] receive the planning target");
    // pathFinding(_start_pt, target_pt); 
}

void simPoseCallback(const geometry_msgs::PoseStamped & msg)
{
    _start_pt(0) = msg.pose.position.x;
    _start_pt(1) = msg.pose.position.y;
    actual_height = msg.pose.position.z;
    if(plan_required && _astar_path_finder->arrived_approx(_start_pt,*(_target_pt.begin())))
    {
        arrived_part_target = true;
    }
    // ROS_WARN("GET POSE");
}

void planCallback(const std_msgs::Bool::ConstPtr & msg)
{
    plan_required = msg->data;
}

bool pathFinding(const Vector2d start_pt, const Vector2d target_pt)
{
    //Call A* to search for a path
    cout<<"[Astar] PathFinding start."<<endl;
    if(!_astar_path_finder->AstarGraphSearch(start_pt, target_pt)) return 0;

    //Retrieve the path

    auto grid_path     = _astar_path_finder->getPath();
    auto grid_twist    = _astar_path_finder->getTwist3();
    // auto visited_nodes = _astar_path_finder->getVisitedNodes();
    if(grid_path_global.size()!=0)
    {
        grid_path_global.push_back(*(grid_path_global.end()-1));
        grid_path_global.push_back(*(grid_path_global.end()-1));
        grid_path_global.push_back(*(grid_path_global.end()-1));
        grid_path_global.push_back(*(grid_path_global.end()-1));
    }
    if(grid_twist_global.size()!=0)
    {
        grid_twist_global.push_back(*(grid_twist_global.end()-1));
        grid_twist_global.push_back(*(grid_twist_global.end()-1));
        grid_twist_global.push_back(*(grid_twist_global.end()-1));
        grid_twist_global.push_back(*(grid_twist_global.end()-1));
    }
    grid_path_global.insert(grid_path_global.end(),grid_path.begin(),grid_path.end());
    grid_twist_global.insert(grid_twist_global.end(),grid_twist.begin(),grid_twist.end());



    //Reset map for next call
    _astar_path_finder->resetUsedGrids();
    return 1;
}
/*----------------------main----------------------*/
int main(int argc, char** argv)
{
//init
    ros::init(argc, argv, "astar_node");
    ros::NodeHandle nh("~");

//sub
    _map_sub  = nh.subscribe( "map",  10, rcvPointCloudCallBack );
    _pts_sub     = nh.subscribe( "waypoints", 1, rcvWaypointsCallback );   
    _nav_sub    = nh.subscribe( "pose", 1, simPoseCallback );
    _plan_sub  = nh.subscribe("/planning_required",1,planCallback);
//pub
    _grid_map_vis_pub             = nh.advertise<sensor_msgs::PointCloud2>("grid_map_vis", 1);
    _grid_path_vis_pub            = nh.advertise<visualization_msgs::Marker>("grid_path_vis", 1);
    _visited_nodes_vis_pub        = nh.advertise<visualization_msgs::Marker>("visited_nodes_vis",1);
    _grid_path_pub                     = nh.advertise<nav_msgs::Path>("grid_path",1);
    _grid_twist_pub                     = nh.advertise<nav_msgs::Path>("grid_twist",1);
    _arrived_pub                           = nh.advertise<std_msgs::Int32>("target_arrived",1);
    

    nh.param("map/resolution",    _resolution,   0.2);
    nh.param("map/x_size"         ,        _x_size, 50.0);
    nh.param("map/y_size"         ,        _y_size, 50.0);    
    nh.param("planning/start_x",  _start_pt(0),  0.0);
    nh.param("planning/start_y",  _start_pt(1),  0.0);//QUES 是否每次从0开始运行？

    _map_lower  << - _x_size/2.0,  -  _y_size/2.0;
    _map_upper << + _x_size/2.0, + _y_size/2.0;
    _inv_resolution = 1.0 / _resolution;
    _max_x_id = (int)(_x_size * _inv_resolution);
    _max_y_id = (int)(_y_size * _inv_resolution);

    _astar_path_finder  = new AstarPathFinder2d();
    _astar_path_finder  -> initGridMap(_resolution, _map_lower, _map_upper, _max_x_id, _max_y_id);
    
//ROS
    ros::Rate rate(20.0);
    while(ros::ok()) 
    {
        // ROS_INFO("test");
        ros::spinOnce();      
        rate.sleep();
    }
    delete _astar_path_finder;
    return 0;
}

void pubGridPath(vector<Vector2d> nodes){
    nav_msgs::Path node_nav;
    node_nav.header.frame_id = "t265_odom_frame";
    geometry_msgs::PoseStamped pt;
    for(int i = 0; i < int(nodes.size()); i++)
    {
        Vector2d coord = nodes[i];
        pt.header.seq = i+1;
        pt.pose.position.x = coord(0);
        pt.pose.position.y = coord(1);
        pt.pose.position.z =  0;

        node_nav.poses.push_back(pt);
    }
    
    _grid_path_pub.publish(node_nav);
    
}
void pubGridTwist(vector<Vector2d> nodes){
    nav_msgs::Path node_nav;
    node_nav.header.frame_id = "t265_odom_frame";
    geometry_msgs::PoseStamped pt;
    for(int i = 0; i < int(nodes.size()); i++)
    {
        Vector2d coord = nodes[i];
        pt.header.seq = i+1;
        pt.pose.position.x = coord(0);
        pt.pose.position.y = coord(1);
        pt.pose.position.z =  0;

        node_nav.poses.push_back(pt);
    }
    
    _grid_twist_pub.publish(node_nav);
    
}


void visGridPath( vector<Vector2d> nodes, bool is_use_jps )
{   
    visualization_msgs::Marker node_vis; 
    node_vis.header.frame_id = "t265_odom_frame";
    node_vis.header.stamp = ros::Time::now();
    
    if(is_use_jps)
        node_vis.ns = "demo_node/jps_path";
    else
        node_vis.ns = "demo_node/astar_path";

    node_vis.type = visualization_msgs::Marker::CUBE_LIST;
    node_vis.action = visualization_msgs::Marker::ADD;
    node_vis.id = 0;

    node_vis.pose.orientation.x = 0.0;
    node_vis.pose.orientation.y = 0.0;
    node_vis.pose.orientation.z = 0.0;
    node_vis.pose.orientation.w = 1.0;

    if(is_use_jps){
        node_vis.color.a = 1.0;
        node_vis.color.r = 1.0;
        node_vis.color.g = 0.0;
        node_vis.color.b = 0.0;
    }
    else{
        node_vis.color.a = 1.0;
        node_vis.color.r = 0.0;
        node_vis.color.g = 1.0;
        node_vis.color.b = 0.0;
    }


    node_vis.scale.x = _resolution;
    node_vis.scale.y = _resolution;
    node_vis.scale.z = _resolution;

    geometry_msgs::Point pt;
    for(int i = 0; i < int(nodes.size()); i++)
    {
        Vector2d coord = nodes[i];
        pt.x = coord(0);
        pt.y = coord(1);
        pt.z = 0;

        node_vis.points.push_back(pt);
    }

    _grid_path_vis_pub.publish(node_vis);
}

void visVisitedNode( vector<Vector2d> nodes )
{   
    visualization_msgs::Marker node_vis; 
    node_vis.header.frame_id = "t265_odom_frame";
    node_vis.header.stamp = ros::Time::now();
    node_vis.ns = "demo_node/expanded_nodes";
    node_vis.type = visualization_msgs::Marker::CUBE_LIST;
    node_vis.action = visualization_msgs::Marker::ADD;
    node_vis.id = 0;

    node_vis.pose.orientation.x = 0.0;
    node_vis.pose.orientation.y = 0.0;
    node_vis.pose.orientation.z = 0.0;
    node_vis.pose.orientation.w = 1.0;
    node_vis.color.a = 0.5;
    node_vis.color.r = 0.0;
    node_vis.color.g = 0.0;
    node_vis.color.b = 1.0;

    node_vis.scale.x = _resolution;
    node_vis.scale.y = _resolution;
    node_vis.scale.z = _resolution;

    geometry_msgs::Point pt;
    for(int i = 0; i < int(nodes.size()); i++)
    {
        Vector2d coord = nodes[i];
        pt.x = coord(0);
        pt.y = coord(1);
        pt.z = 0;

        node_vis.points.push_back(pt);
    }

    _visited_nodes_vis_pub.publish(node_vis);
}