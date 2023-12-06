#include "kdl_ros_control/kdl_robot.h"
#include "kdl_ros_control/kdl_control.h"
#include "kdl_ros_control/kdl_planner.h"

#include "kdl_parser/kdl_parser.hpp"
#include "urdf/model.h"
#include <std_srvs/Empty.h>
#include "eigen_conversions/eigen_kdl.h"

#include "ros/ros.h"
#include "std_msgs/Float64.h"
#include "sensor_msgs/JointState.h"
#include "geometry_msgs/PoseStamped.h"
#include "gazebo_msgs/SetModelConfiguration.h"


// Global variables
std::vector<double> jnt_pos(7,0.0), init_jnt_pos(7,0.0), jnt_vel(7,0.0), aruco_pose(7,0.0);
bool robot_state_available = false, aruco_pose_available = false;
double lambda = 10*0.2;
double KP = 15;

// Functions
KDLRobot createRobot(std::string robot_string)
{
    KDL::Tree robot_tree;
    urdf::Model my_model;
    if (!my_model.initFile(robot_string))
    {
        printf("Failed to parse urdf robot model \n");
    }
    if (!kdl_parser::treeFromUrdfModel(my_model, robot_tree))
    {
        printf("Failed to construct kdl tree \n");
    }
    
    KDLRobot robot(robot_tree);
    return robot;
}

void jointStateCallback(const sensor_msgs::JointState & msg)
{
    robot_state_available = true;
    // Update joints
    jnt_pos.clear();
    jnt_vel.clear();
    for (int i = 0; i < msg.position.size(); i++)
    {
        jnt_pos.push_back(msg.position[i]);
        jnt_vel.push_back(msg.velocity[i]);
    }
}

void arucoPoseCallback(const geometry_msgs::PoseStamped & msg)
{
    aruco_pose_available = true;
    aruco_pose.clear();
    aruco_pose.push_back(msg.pose.position.x);
    aruco_pose.push_back(msg.pose.position.y);
    aruco_pose.push_back(msg.pose.position.z);
    aruco_pose.push_back(msg.pose.orientation.x);
    aruco_pose.push_back(msg.pose.orientation.y);
    aruco_pose.push_back(msg.pose.orientation.z);
    aruco_pose.push_back(msg.pose.orientation.w);
}


// Main
int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("Please, provide a path to a URDF file...\n");
        return 0;
    }

    // Init node
    ros::init(argc, argv, "kdl_ros_control_node");
    ros::NodeHandle n;

    // Rate
    ros::Rate loop_rate(500);

    // Subscribers
    ros::Subscriber aruco_pose_sub = n.subscribe("/aruco_single/pose", 1, arucoPoseCallback);
    ros::Subscriber joint_state_sub = n.subscribe("/iiwa/joint_states", 1, jointStateCallback);

    // Publishers
    ros::Publisher joint1_dq_pub = n.advertise<std_msgs::Float64>("/iiwa/VelocityJointInterface_J1_controller/command", 1);
    ros::Publisher joint2_dq_pub = n.advertise<std_msgs::Float64>("/iiwa/VelocityJointInterface_J2_controller/command", 1);
    ros::Publisher joint3_dq_pub = n.advertise<std_msgs::Float64>("/iiwa/VelocityJointInterface_J3_controller/command", 1);
    ros::Publisher joint4_dq_pub = n.advertise<std_msgs::Float64>("/iiwa/VelocityJointInterface_J4_controller/command", 1);
    ros::Publisher joint5_dq_pub = n.advertise<std_msgs::Float64>("/iiwa/VelocityJointInterface_J5_controller/command", 1);
    ros::Publisher joint6_dq_pub = n.advertise<std_msgs::Float64>("/iiwa/VelocityJointInterface_J6_controller/command", 1);
    ros::Publisher joint7_dq_pub = n.advertise<std_msgs::Float64>("/iiwa/VelocityJointInterface_J7_controller/command", 1); 
    
    //Publisher s commands
    ros::Publisher s1 = n.advertise<std_msgs::Float64>("/iiwa/s1", 1);    
    ros::Publisher s2 = n.advertise<std_msgs::Float64>("/iiwa/s2", 1);    
    ros::Publisher s3 = n.advertise<std_msgs::Float64>("/iiwa/s3", 1);    


    // Services
    ros::ServiceClient robot_set_state_srv = n.serviceClient<gazebo_msgs::SetModelConfiguration>("/gazebo/set_model_configuration");
    ros::ServiceClient pauseGazebo = n.serviceClient<std_srvs::Empty>("/gazebo/pause_physics");

    // Initial desired robot state
    init_jnt_pos[0] = 0.0;
    init_jnt_pos[1] = 1.57;
    init_jnt_pos[2] = -1.57;
    init_jnt_pos[3] = -1.57;
    init_jnt_pos[4] = 1.57;
    init_jnt_pos[5] = -1.57;
    init_jnt_pos[6] = +1.57;
    Eigen::VectorXd qdi = toEigen(init_jnt_pos);

    // Create robot
    KDLRobot robot = createRobot(argv[1]);

    // Messages
    std_msgs::Float64 dq1_msg, dq2_msg, dq3_msg, dq4_msg, dq5_msg, dq6_msg, dq7_msg;
    std_srvs::Empty pauseSrv;

    // s messages
    std_msgs::Float64 s1_msg,s2_msg,s3_msg;

    // Joints
    KDL::JntArray qd(robot.getNrJnts()), dqd(robot.getNrJnts()), ddqd(robot.getNrJnts());
    qd.data.setZero();
    dqd.data.setZero();
    ddqd.data.setZero();

    // Wait for robot and object state
    while (!(robot_state_available))
    {
        ROS_INFO_STREAM_ONCE("Robot/object state not available yet.");
        ROS_INFO_STREAM_ONCE("Please start gazebo simulation.");
        
        ros::spinOnce();
        loop_rate.sleep();
    }
    
    // Bring robot in a desired initial configuration with velocity control
    ROS_INFO("Robot going into initial configuration....");
    double jnt_position_error_norm = computeJointErrorNorm(toEigen(init_jnt_pos),toEigen(jnt_pos));
    while (jnt_position_error_norm > 0.01)
    {
        dqd.data = KP*(toEigen(init_jnt_pos) - toEigen(jnt_pos));

        // Set joints
        dq1_msg.data = dqd.data[0];
        dq2_msg.data = dqd.data[1];
        dq3_msg.data = dqd.data[2];
        dq4_msg.data = dqd.data[3];
        dq5_msg.data = dqd.data[4];
        dq6_msg.data = dqd.data[5];
        dq7_msg.data = dqd.data[6];

        // Publish
        joint1_dq_pub.publish(dq1_msg);
        joint2_dq_pub.publish(dq2_msg);
        joint3_dq_pub.publish(dq3_msg);
        joint4_dq_pub.publish(dq4_msg);
        joint5_dq_pub.publish(dq5_msg);
        joint6_dq_pub.publish(dq6_msg);
        joint7_dq_pub.publish(dq7_msg);

        jnt_position_error_norm = computeJointErrorNorm(toEigen(init_jnt_pos),toEigen(jnt_pos));
        std::cout << "jnt_position_error_norm: " << jnt_position_error_norm << "\n" << std::endl;
        ros::spinOnce();
        loop_rate.sleep();

    }

    // Specify an end-effector: camera in flange transform
    KDL::Frame ee_T_cam;
    ee_T_cam.M = KDL::Rotation::RotY(1.57)*KDL::Rotation::RotZ(-1.57);
    ee_T_cam.p = KDL::Vector(0,0,0.025);
    robot.addEE(ee_T_cam);

    // Update robot
    robot.update(jnt_pos, jnt_vel);

    // Retrieve initial simulation time
    ros::Time begin = ros::Time::now();
    ROS_INFO_STREAM_ONCE("Starting control loop ...");

    // Retrieve initial ee pose
    KDL::Frame Fi = robot.getEEFrame();
    Eigen::Vector3d pdi = toEigen(Fi.p);

    while (ros::ok())
    {
        if (robot_state_available && aruco_pose_available)
        {
            // Update robot
            robot.update(jnt_pos, jnt_vel);
        
            // Update time
            double t = (ros::Time::now()-begin).toSec();
            std::cout << "time: " << t << std::endl;

            // compute current jacobians
            KDL::Jacobian J_cam = robot.getEEJacobian();
            KDL::Frame cam_T_object(KDL::Rotation::Quaternion(aruco_pose[3], aruco_pose[4], aruco_pose[5], aruco_pose[6]), KDL::Vector(aruco_pose[0], aruco_pose[1], aruco_pose[2]));

            // look at point: compute rotation error from angle/axis
            Eigen::Matrix<double,3,1> aruco_pos_n = toEigen(cam_T_object.p);        //(aruco_pose[0],aruco_pose[1],aruco_pose[2]);
            aruco_pos_n.normalize();
            Eigen::Vector3d r_o = skew(Eigen::Vector3d(0,0,1)) * aruco_pos_n;
            double aruco_angle = std::acos(Eigen::Vector3d(0,0,1).dot(aruco_pos_n));
            KDL::Rotation Re = KDL::Rotation::Rot(KDL::Vector(r_o[0], r_o[1], r_o[2]), aruco_angle);

            // compute errors
            Eigen::Matrix<double,6,1> x_tilde = Eigen::Matrix<double,6,1> :: Zero(); 

            // commentare per 2.A
            // Eigen::Matrix<double,3,1> e_o = computeOrientationError(toEigen(robot.getEEFrame().M * Re), toEigen(robot.getEEFrame().M));
            // Eigen::Matrix<double,3,1> e_o_w = computeOrientationError(toEigen(Fi.M), toEigen(robot.getEEFrame().M));
            // Eigen::Matrix<double,3,1> e_p = computeLinearError(pdi,toEigen(robot.getEEFrame().p));          
            // x_tilde << e_p,  e_o_w[0], e_o[1], e_o[2];                                                      
            // fin qui

            // PUNTO 2.A
            KDL::Frame frame_offset=cam_T_object;
            //la pos è shiftata di 0.5 lungo z
            frame_offset.p = cam_T_object.p - KDL::Vector(0,0,0.5);
            //orient ruotato 180° attorno a x
            frame_offset.M = cam_T_object.M* KDL::Rotation::RotX(-3.14);
            //riporto il frame al frame base
            KDL::Frame base_T_offset = robot.getEEFrame()* frame_offset;
            
            //compute errors
            Eigen::Matrix<double,3,1> e_o = computeOrientationError(toEigen(base_T_offset.M), toEigen(robot.getEEFrame().M));
            Eigen::Matrix<double,3,1> e_o_w = computeOrientationError(toEigen(Fi.M), toEigen(robot.getEEFrame().M));
            Eigen::Matrix<double,3,1> e_p = computeLinearError(toEigen(base_T_offset.p),toEigen(robot.getEEFrame().p));
            x_tilde << e_p,  e_o_w[0], e_o[1], e_o[2];
            // FINE PUNTO 2.A
            

            // resolved velocity control
            Eigen::MatrixXd J_pinv = J_cam.data.completeOrthogonalDecomposition().pseudoInverse();
            // legge di controllo velocita vecchia
            dqd.data = lambda*J_pinv*x_tilde + 10*(Eigen::Matrix<double,7,7>::Identity() - J_pinv*J_cam.data)*(qdi - toEigen(jnt_pos));



            // PUNTO 2.B
            Eigen::Matrix<double,3,1> cPo = toEigen(cam_T_object.p);
            Eigen::Matrix<double,3,1> s = cPo/cPo.norm();       // unit-norm axis

            // The following matrix maps linear/angular velocities of the camera to changes in s
            Eigen::Matrix<double,3,3> Rc = toEigen(robot.getEEFrame().M);
            Eigen::Matrix<double,6,6> R_tot = Eigen::Matrix<double,6,6>::Zero();
            R_tot.block(0,0,3,3) = Rc;
            R_tot.block(3,3,3,3) = Rc;

            Eigen::Matrix<double,3,6> L = Eigen::Matrix<double,3,6>::Zero();
            Eigen::Matrix<double,3,3> L1 = (-1/cPo.norm()) * (Eigen::Matrix<double,3,3>::Identity() - s*s.transpose());
            Eigen::Matrix<double,3,3> L2 = skew(s);
            L.block(0,0,3,3) = L1;
            L.block(0,3,3,3) = L2;

            L = L * (R_tot.transpose());
            Eigen::MatrixXd L_J = L * toEigen(J_cam);
            Eigen::MatrixXd L_J_pinv = L_J.completeOrthogonalDecomposition().pseudoInverse();
            Eigen::MatrixXd N = (Eigen::Matrix<double,7,7>::Identity() - (L_J_pinv * L_J));

            // legge di controllo nuova 2.B
            //dqd.data = lambda * L_J_pinv * Eigen::Vector3d(0,0,1) + 10 * N * (qdi - toEigen(jnt_pos));
            // FINE PUNTO 2.B

            // debug
            // std::cout << "e_p: " << std::endl << e_p << std::endl;
            // std::cout << "x_tilde: " << std::endl << x_tilde << std::endl;
            // std::cout << "Rd: " << std::endl << toEigen(robot.getEEFrame().M*Re) << std::endl;
            // std::cout << "aruco_pos_n: " << std::endl << aruco_pos_n << std::endl;
            // std::cout << "aruco_pos_n.norm(): " << std::endl << aruco_pos_n.norm() << std::endl;
            // std::cout << "Re: " << std::endl << Re << std::endl;
            // std::cout << "jacobian: " << std::endl << robot.getEEJacobian().data << std::endl;
            // std::cout << "jsim: " << std::endl << robot.getJsim() << std::endl;
            // std::cout << "c: " << std::endl << robot.getCoriolis().transpose() << std::endl;
            // std::cout << "g: " << std::endl << robot.getGravity().transpose() << std::endl;
            // std::cout << "qd: " << std::endl << qd.data.transpose() << std::endl;
            // std::cout << "q: " << std::endl << robot.getJntValues().transpose() << std::endl;
            // std::cout << "tau: " << std::endl << tau.transpose() << std::endl;
            // std::cout << "desired_pose: " << std::endl << des_pose << std::endl;
            // std::cout << "current_pose: " << std::endl << robot.getEEFrame() << std::endl;

        // publish s messages   
        s1_msg.data = s[0];
        s2_msg.data = s[1];
        s3_msg.data = s[2];

        s1.publish(s1_msg);
        s2.publish(s2_msg);
        s3.publish(s3_msg);
        
        }
        else{
            dqd.data = KP*(toEigen(init_jnt_pos) - toEigen(jnt_pos));
        }
        // Set joints
        dq1_msg.data = dqd.data[0];
        dq2_msg.data = dqd.data[1];
        dq3_msg.data = dqd.data[2];
        dq4_msg.data = dqd.data[3];
        dq5_msg.data = dqd.data[4];
        dq6_msg.data = dqd.data[5];
        dq7_msg.data = dqd.data[6];

        // Publish
        joint1_dq_pub.publish(dq1_msg);
        joint2_dq_pub.publish(dq2_msg);
        joint3_dq_pub.publish(dq3_msg);
        joint4_dq_pub.publish(dq4_msg);
        joint5_dq_pub.publish(dq5_msg);
        joint6_dq_pub.publish(dq6_msg);
        joint7_dq_pub.publish(dq7_msg);



        ros::spinOnce();
        loop_rate.sleep();
    }

    return 0;
}