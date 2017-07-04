/**
* @file     : get_board_position.cpp
* @brief    : publish the position of 10 boards.
* @author   : libn
* @time     : Sep 17, 2016 9:50:58 PM
*/

#include <ros/ros.h>
#include <std_msgs/Int32.h>

#include <sensor_msgs/LaserScan.h>
#include <state_machine/DrawingBoard.h>
#include <state_machine/DrawingBoard10.h>
#include <geometry_msgs/PoseStamped.h>

/********************** 发布 **********************/

/* 显示屏数字 */
std_msgs::Int32 vision_num_data;
std_msgs::Int32 vision_num_data_last;
ros::Publisher vision_num_pub;

/* 喷绘板数字及位置 */
/* send indexed setpoint. -libn <Aug 15, 2016 9:00:02 AM> */
state_machine::DrawingBoard board; //某一个喷绘板的信息
state_machine::DrawingBoard10 board10; /* current board10 */ //所有喷绘板的信息
state_machine::DrawingBoard10 board10_last; /* last board10 */
state_machine::DrawingBoard10 board10_pub; /* board10 for publish */
ros::Publisher DrawingBoard_Position_pub;

/********************** 订阅 **********************/

/* 飞行器位置 */
geometry_msgs::PoseStamped current_pos;
ros::Subscriber local_pos_sub;
void pos_cb(const geometry_msgs::PoseStamped::ConstPtr& msg)
{
    current_pos = *msg;
}

/* 相机工作模式的切换：不工作or显示屏or喷绘板 */
std_msgs::Int32 camera_switch_data;
ros::Subscriber camera_switch_sub;
void camera_switch_cb(const std_msgs::Int32::ConstPtr& msg)
{
    camera_switch_data = *msg;
//    ROS_INFO("get camera_switch_data = %d",camera_switch_data.data);
}

/* 任何视觉信息都通过该msg传输 */
sensor_msgs::LaserScan board_scan;
ros::Subscriber board_pos_sub;
#define MIN_OBSERVE_TIMES 4 /* 4 times. */
#define MIN_DETECTION_TIMES_FAR 4  /* count_num > MIN_DETECTION_TIMES => num detected; else: num not detected. */
#define MIN_DETECTION_TIMES_NEAR 2
#define MAX_DETECTION_DISTANCE 0.5  /* max detected board distance between different loops. */
int count_num = 0;
int count_detection[10] = {0,0,0,0,0,0,0,0,0,0};
void board_pos_cb(const sensor_msgs::LaserScan::ConstPtr& msg)
{
	board_scan = *msg;
//    ROS_INFO("vision message received!");

    int num;
    /*  camera_switch: 0: mission closed;
     *                 1: vision_one_num_get;
     *                 2: vision_num_scan. -libn */
    // 1. 显示屏信息
    if(camera_switch_data.data == 1 && board_scan.ranges[1] > 100 && board_scan.ranges[2] > 100)
    {
        num = (int)board_scan.ranges[0];
        if(num == 11)
        {
            ROS_INFO("incomplete rectangle detected");
        }
        else if(num >9 || num <0)
        {
            ROS_INFO("board num error!");
        }
        else
        {
            vision_num_data.data = (int)num;
            if(vision_num_data_last.data == vision_num_data.data)   count_num++;
            else    count_num = 0;
            vision_num_data_last = vision_num_data;
            if(count_num >= MIN_OBSERVE_TIMES) /* get the same num for MIN_DETECTION_TIMES times at last. */
            {
                vision_num_pub.publish(vision_num_data); //发布显示屏数字
//                ROS_INFO("vision_num_data = %d",vision_num_data.data);
                count_num = 0;
            }
        }
    }
    // 2. 喷绘板视觉信息
    if(camera_switch_data.data == 2 && board_scan.ranges[1] < 100 && board_scan.ranges[2] < 100)
    {
        int amout = board_scan.ranges.size()/4;
        /* get vision current detection message. */
        for ( int i = 0; i < amout; ++i )
        {
            num = (int)board_scan.ranges[i*4]; /* No. of board detected. -libn */
            if(num == 11)
            {
                break;	/* incomplete rectangle detected. -libn */
            }
            else if(num >9 || num <0)
            {
                ROS_INFO("board num error!");
                break;
            }
//            ROS_INFO("data received:\n"
//                     "num = %d "
//                     "x = %f "
//                     "y = %f "
//                     "z = %f",
//                     num,board_scan.ranges[i*4 + 1],board_scan.ranges[i*4 + 1],board_scan.ranges[i*4 + 1]);
            board10.drawingboard[num].num = num; //喷绘板数字
            board10.drawingboard[num].x = board_scan.ranges[i*4 + 1] + current_pos.pose.position.x; //喷绘板位置（绝对位置）
            board10.drawingboard[num].y = board_scan.ranges[i*4 + 2] + current_pos.pose.position.y;
            board10.drawingboard[num].z = board_scan.ranges[i*4 + 3] + current_pos.pose.position.z;
            board10.drawingboard[num].valid = true; //喷绘板可见

            /* get the same position for MIN_DETECTION_TIMES times at last. */
            if(fabs(board10.drawingboard[num].x - board10_last.drawingboard[num].x) < MAX_DETECTION_DISTANCE
                 && fabs(board10.drawingboard[num].y - board10_last.drawingboard[num].y) < MAX_DETECTION_DISTANCE)
            {
                count_detection[num]++;
            }
            else
            {
                count_detection[num] = 0;
            }
            float distance = sqrt(board10.drawingboard[num].x*board10.drawingboard[num].x+board10.drawingboard[num].y*board10.drawingboard[num].y);
            if(distance >4) //距离喷绘板较远
            {
                if(count_detection[num] >= MIN_DETECTION_TIMES_FAR)
                {
                    /* store only stable vision message. */
                    board10_pub.drawingboard[num] = board10.drawingboard[num];
                    count_detection[num] = 0;
                }
            }
            else //距离喷绘板较近
            {
                if(count_detection[num] >= MIN_DETECTION_TIMES_NEAR)
                {
                    /* store only stable vision message. */
                    board10_pub.drawingboard[num] = board10.drawingboard[num];
                    count_detection[num] = 0;
                }
            }


        }

        /* display and publish stable vision message. */
        board10_last = board10;
        DrawingBoard_Position_pub.publish(board10_pub);
//        ROS_INFO("current pos:\n"
//                "x = %5.3f y = %5.3f z = %5.3f\n",
//                current_pos.pose.position.x,current_pos.pose.position.y,current_pos.pose.position.z);
//        ROS_INFO("\nboard_0 position: %d x = %f y = %f z = %f\n"
//                    "board_1 position: %d x = %f y = %f z = %f\n"
//                    "board_2 position: %d x = %f y = %f z = %f\n"
//                    "board_3 position: %d x = %f y = %f z = %f\n"
//                    "board_4 position: %d x = %f y = %f z = %f\n"
//                    "board_5 position: %d x = %f y = %f z = %f\n"
//                    "board_6 position: %d x = %f y = %f z = %f\n"
//                    "board_7 position: %d x = %f y = %f z = %f\n"
//                    "board_8 position: %d x = %f y = %f z = %f\n"
//                    "board_9 position: %d x = %f y = %f z = %f\n",
//                    board10_pub.drawingboard[0].valid,board10_pub.drawingboard[0].x,board10_pub.drawingboard[0].y,board10_pub.drawingboard[0].z,
//                    board10_pub.drawingboard[1].valid,board10_pub.drawingboard[1].x,board10_pub.drawingboard[1].y,board10_pub.drawingboard[1].z,
//                    board10_pub.drawingboard[2].valid,board10_pub.drawingboard[2].x,board10_pub.drawingboard[2].y,board10_pub.drawingboard[2].z,
//                    board10_pub.drawingboard[3].valid,board10_pub.drawingboard[3].x,board10_pub.drawingboard[3].y,board10_pub.drawingboard[3].z,
//                    board10_pub.drawingboard[4].valid,board10_pub.drawingboard[4].x,board10_pub.drawingboard[4].y,board10_pub.drawingboard[4].z,
//                    board10_pub.drawingboard[5].valid,board10_pub.drawingboard[5].x,board10_pub.drawingboard[5].y,board10_pub.drawingboard[5].z,
//                    board10_pub.drawingboard[6].valid,board10_pub.drawingboard[6].x,board10_pub.drawingboard[6].y,board10_pub.drawingboard[6].z,
//                    board10_pub.drawingboard[7].valid,board10_pub.drawingboard[7].x,board10_pub.drawingboard[7].y,board10_pub.drawingboard[7].z,
//                    board10_pub.drawingboard[8].valid,board10_pub.drawingboard[8].x,board10_pub.drawingboard[8].y,board10_pub.drawingboard[8].z,
//                    board10_pub.drawingboard[9].valid,board10_pub.drawingboard[9].x,board10_pub.drawingboard[9].y,board10_pub.drawingboard[9].z);
    }
}

int main(int argc, char **argv)
{
	ROS_INFO("I was alive.");
	ros::init(argc, argv, "get_board_pos");
	ros::NodeHandle nh;

    // For 发布，给句柄赋值
    vision_num_pub  = nh.advertise<std_msgs::Int32>("vision_num", 10);
    board10.drawingboard.resize(10);		/* MUST! -libn */
    DrawingBoard_Position_pub = nh.advertise<state_machine::DrawingBoard10>("DrawingBoard_Position10", 1);

    // For 订阅，指明回调函数
    board_pos_sub = nh.subscribe<sensor_msgs::LaserScan>("/vision/digit_nws_position", 10, board_pos_cb);
    local_pos_sub = nh.subscribe<geometry_msgs::PoseStamped>("mavros/local_position/pose", 10, pos_cb);
    camera_switch_sub = nh.subscribe<std_msgs::Int32>("camera_switch", 10, camera_switch_cb);

	for(int i = 0; i < 10; i++)
	{
        board10.drawingboard[i].num = 66;
        board10.drawingboard[i].x = 0.0f;
        board10.drawingboard[i].y = 0.0f;
        board10.drawingboard[i].z = 0.0f;
        board10.drawingboard[i].valid = false;
	}
    board10_last = board10;
    board10_pub = board10;

	ros::spin();
	return 0;
}
