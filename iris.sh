#! /usr/bin/env bash
source devel/setup.bash
roslaunch fsm iris_realsense_camera_px4_mavros_vo.launch

wait 
exit 0
