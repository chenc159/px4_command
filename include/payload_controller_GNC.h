#ifndef PAYLOAD_CONTROLLER_GNC_H
#define PAYLOAD_CONTROLLER_GNC_H

#include <Eigen/Eigen>
#include <math.h>
#include <iostream>
#include <string>
#include <command_to_mavros.h>
#include <px4_command_utils.h>
#include <math_utils.h>
#include <px4_command/DroneState.h>
#include <px4_command/TrajectoryPoint.h>
#include <px4_command/AttitudeReference.h>
#include <px4_command/ControlOutput.h>
#include <px4_command/ControlParameter.h>
#include <px4_command/AuxiliaryState.h>
#include <px4_command/Emergency.h>
#include <px4_command/FleetStatus.h>
#include <px4_command/AddonForce.h>
using std::string;
using std::iostream;
class payload_controller_GNC
{
    public:
        payload_controller_GNC(char drone_ID[20],ros::NodeHandle& main_handle) 
        {
            // use drone ID to get the correct name for parameters
            uav_pref = "uav";
            uav_pref = uav_pref + drone_ID[0];
            // load uav and payload parameters
            main_handle.param<float>(uav_pref + "_Pos_GNC/mass", Quad_MASS, 1.0);
            main_handle.param<float>(uav_pref + "_Pos_GNC/cablelength", Cable_Length, 1.0);
            main_handle.param<int>("Pos_GNC/num_drone",num_drone,1);
            main_handle.param<float>(uav_pref + "_Pos_GNC/TetherOffset_x", TetherOffset(0), 0.5);
            main_handle.param<float>(uav_pref + "_Pos_GNC/TetherOffset_y", TetherOffset(1), 0);
            main_handle.param<float>(uav_pref + "_Pos_GNC/TetherOffset_z", TetherOffset(2), 0);
            main_handle.param<double>(uav_pref + "_Pos_GNC/motor_slope", motor_slope,0.3);
            main_handle.param<double>(uav_pref + "_Pos_GNC/motor_intercept", motor_intercept, 0);
            main_handle.param<float>(uav_pref + "_Pos_GNC/PayloadSharingPortion", PayloadSharingPortion, 0.5);
            main_handle.param<float>("Payload/mass", Payload_Mass, 1.0);
            main_handle.param<bool>("Pos_GNC/PubAuxiliaryState", isPubAuxiliaryState, true);
            main_handle.param<bool>("Pos_GNC/UseAddonForce", isAddonForcedUsed, false);
            TetherOffsetCross = Hatmap(TetherOffset);
            D.setZero();
            Eigen::Vector3f temp_offset;
            float e_n = 1.0;
            float a_j;
            for (int i = 0; i< num_drone; i++) {
                temp_offset.setZero();
                a_j = 0;
                main_handle.param<float>("uav" + to_string(i) + "_Pos_GNC/PayloadSharingPortion", a_j, 0.5);
                main_handle.param<float>("uav" + to_string(i) + "_Pos_GNC/TetherOffset_x", temp_offset(0), 0.5);
                main_handle.param<float>("uav" + to_string(i) + "_Pos_GNC/TetherOffset_y", temp_offset(1), 0);
                main_handle.param<float>("uav" + to_string(i) + "_Pos_GNC/TetherOffset_z", temp_offset(2), 0);
                D += a_j * Hatmap(temp_offset) * Hatmap(temp_offset);
                e_n *= temp_offset.norm();
            }
            if (num_drone < 3) 
            {
                // 
                Ej = -  TetherOffsetCross / e_n;
            } else {
                Ej = TetherOffsetCross * D.inverse(); // Ej = t_jx D^-1 if at least 3 drones are used.
            }
            Cable_Length_sq = Cable_Length * Cable_Length;
            TotalLiftedMass = Payload_Mass* PayloadSharingPortion + Quad_MASS;
            // load control gains
            kv<< 1.0,0.0,0.0,
                   0.0,1.0,0.0,
                   0.0,0.0,1.0;
            kR<< 1.0,0.0,0.0,
                   0.0,1.0,0.0,
                   0.0,0.0,1.0;
            Kphi <<1.0,0.0,0.0,
                   0.0,1.0,0.0,
                   0.0,0.0,1.0;    
            kvi << 1.0,0.0,0.0,
                   0.0,1.0,0.0,
                   0.0,0.0,1.0;
            kRi << 1.0,0.0,0.0,
                   0.0,1.0,0.0,
                   0.0,0.0,1.0;
            main_handle.param<float>("Pos_GNC/kv_xy", kv(0,0), 0.2);
            main_handle.param<float>("Pos_GNC/kv_xy", kv(1,1), 0.2);
            main_handle.param<float>("Pos_GNC/Kv_z",  kv(2,2), 0.4);

            main_handle.param<float>("Pos_GNC/kR_xy" , kR(0,0), 0.2);
            main_handle.param<float>("Pos_GNC/kR_xy" , kR(1,1), 0.2);
            main_handle.param<float>("Pos_GNC/kR_z"  , kR(2,2), 0.4);

            main_handle.param<float>("Pos_GNC/kvi_xy" , kvi(0,0), 0.02);
            main_handle.param<float>("Pos_GNC/kvi_xy" , kvi(1,1), 0.02);
            main_handle.param<float>("Pos_GNC/kvi_z"  , kvi(2,2), 0.04);

            main_handle.param<float>("Pos_GNC/kL"     ,  kL, 0.5);
            main_handle.param<float>("Pos_GNC/Kphi_xy",  Kphi(0,0), 1);
            main_handle.param<float>("Pos_GNC/Kphi_xy",  Kphi(1,1), 1);
            main_handle.param<float>("Pos_GNC/Kphi_z" ,  Kphi(2,2), 1);

            main_handle.param<float>("Pos_GNC/kRi_xy",  kRi(0,0), 0.02);
            main_handle.param<float>("Pos_GNC/kRi_xy",  kRi(1,1), 0.02);
            main_handle.param<float>("Pos_GNC/kRi_z" ,  kRi(2,2), 0.02);


            main_handle.param<float>("Limit/pxy_error_max", pos_error_max[0], 0.6);
            main_handle.param<float>("Limit/pxy_error_max", pos_error_max[1], 0.6);
            main_handle.param<float>("Limit/pz_error_max" , pos_error_max[2], 1.0);

            main_handle.param<float>("Pos_GNC/pos_int_max_x"  ,  pos_int_max(0), 1);
            main_handle.param<float>("Pos_GNC/pos_int_max_y"  ,  pos_int_max(1), 1);
            main_handle.param<float>("Pos_GNC/pos_int_max_z"  ,  pos_int_max(2), 1);

            main_handle.param<float>("Pos_GNC/ang_int_max_x"  ,  ang_int_max(0), 1);
            main_handle.param<float>("Pos_GNC/ang_int_max_y"  ,  ang_int_max(1), 1);
            main_handle.param<float>("Pos_GNC/ang_int_max_z"  ,  ang_int_max(2), 1);
            
            main_handle.param<float>("Limit/tilt_max", tilt_max, 20.0);
            main_handle.param<float>("Pos_GNC/pos_int_start_error"  , pos_int_start_error, 0.3);
            main_handle.param<float>("Pos_GNC/angle_int_start_error", angle_int_start_error, 0.5);
 
            main_handle.param<float>("Pos_GNC/fp_max_x", fp_max(0),1);
            main_handle.param<float>("Pos_GNC/fp_max_y", fp_max(1),1);
            main_handle.param<float>("Pos_GNC/fp_max_z", fp_max(2),1);
 
            main_handle.param<float>("Pos_GNC/MaxInclination", MaximumInclination , 40.0);
            main_handle.param<float>("Pos_GNC/CableLengthTolerance", Cable_Tolerance, 1.2);

            lambda_j.setZero();
            main_handle.param<float>("Pos_GNC/lambda_j", lambda_j(0,0), 1);
            main_handle.param<float>("Pos_GNC/lambda_j", lambda_j(1,1), 1);
            main_handle.param<float>("Pos_GNC/lambda_j", lambda_j(2,2), 1);

            // special parameters
            Identity << 1.0,0.0,0.0,
                        0.0,1.0,0.0,
                        0.0,0.0,1.0;
            g_I<<0.0,
                 0.0,
                -9.81;
            // initialize states:
            Quad_Drone << 1.0,
                          0.0,
                          0.0,
                          0.0;
            R_Ij << 1.0,0.0,0.0,
                   0.0,1.0,0.0,
                   0.0,0.0,1.0;
            R_IP << 1.0,0.0,0.0,
                   0.0,1.0,0.0,
                   0.0,0.0,1.0;
            R_IPd<< 1.0,0.0,0.0,
                   0.0,1.0,0.0,
                   0.0,0.0,1.0;
            B_j << 1.0,0.0,
                   0.0,1.0,
                   0.0,0.0;
            Delta_j.setZero();
            Delta_j_p.setZero();
            dot_vqj.setZero();
            accel_body.setZero();
            f_L_j.setZero();
            IntegralPose.setZero();
            IntegralAttitude.setZero(); 
            acc_x = 0.0;
            acc_y = 0.0;
            acc_z = 0.0;           
            u_l.setZero();
            u_d.setZero();
            r_j.setZero();
            v_j.setZero();
            rd_j.setZero();
            f_p_j.setZero();
            // robust control related.
            Delta_pt.setZero();
            Delta_rt.setZero();
            R1.setZero();
            R2.setZero();            
            PayloadDisturbance.setZero();
            thrust_sp.setZero();
            throttle_sp.setZero();
            // setting up communication channel:
            if (isPubAuxiliaryState)  {
                pubAuxiliaryState   = main_handle.advertise<px4_command::AuxiliaryState > ("/" + uav_pref + "/px4_command/auxiliarystate", 1000);
            }
            clientSendParameter = main_handle.serviceClient<px4_command::ControlParameter>("/" + uav_pref + "/px4_command/parameters");
            pubFleetStatus      =  main_handle.advertise<px4_command::FleetStatus>("/" + uav_pref + "/px4_command/fleetstatus", 1000);
            subAddonForce       =  main_handle.subscribe<px4_command::AddonForce>("/uav0/px4_command/addonforce", 100, &payload_controller_GNC::GetAddonForce, this);
            //emergencyKill       = main_handle.serviceClient<px4_command::Emergency>("/" + uav_pref + "/px4_command/emergencyKill"); 
            // send the parameters to ground station:
            ParamSrv.request.controllername = uav_pref + " Payload Pos_GNC";
            ParamSrv.request.dronemass   = Quad_MASS;
            ParamSrv.request.cablelength = Cable_Length;
            ParamSrv.request.a_j         = PayloadSharingPortion;
            ParamSrv.request.payloadmass = Payload_Mass;
            ParamSrv.request.num_drone   = num_drone;
            ParamSrv.request.motor_slope = (float)motor_slope;
            ParamSrv.request.motor_intercept = (float)motor_intercept;
            ParamSrv.request.t_jx        = TetherOffset(0);
            ParamSrv.request.t_jy        = TetherOffset(1);
            ParamSrv.request.t_jz        = TetherOffset(2);
            ParamSrv.request.kv_xy       = kv(0,0);
            ParamSrv.request.Kv_z        = kv(2,2);
            ParamSrv.request.kvi_xy      = kvi(0,0);
            ParamSrv.request.kvi_z       = kvi(2,2);
            ParamSrv.request.kR_xy       = kR(0,0);
            ParamSrv.request.kR_z        = kR(2,2);
            ParamSrv.request.kL          = kL;
            ParamSrv.request.Kphi_xy     = Kphi(0,0);
            ParamSrv.request.Kphi_z      = Kphi(2,2);
            ParamSrv.request.pxy_error_max = pos_error_max[0];
            ParamSrv.request.pz_error_max  = pos_error_max[2];
            ParamSrv.request.pxy_int_max = pos_int_max(0);
            ParamSrv.request.pz_int_max  = pos_int_max(2);
            ParamSrv.request.tilt_max    = tilt_max;
            ParamSrv.request.int_start_error = pos_int_start_error;
            ParamSrv.request.fp_max_x = fp_max(0);
            ParamSrv.request.fp_max_y = fp_max(1);
            ParamSrv.request.fp_max_z = fp_max(2);
            // call the ground station to recieve the parameters
            if (clientSendParameter.call(ParamSrv)) {
                ROS_INFO("Parameter sent to ground station");
            } else {
                ROS_WARN("Failed to connect ground station !!! ");
            }            
            // initialize emergency flag
            isEmergency = false;
        }
        //Printf the controller parameter
        void printf_param();
        void printf_result();
        //Determine whether the system should enter an emergency landing
        bool emergency_switch();
        // [Input: Current state, Reference state, sub_mode, dt; Output: AttitudeReference; 
        px4_command::ControlOutput payload_controller(const px4_command::DroneState&      _DroneState, 
                                                      const px4_command::TrajectoryPoint& _Reference_State, 
                                                      float dt);                                            
        // control command 
        Eigen::Vector3d accel_sp;
        px4_command::ControlOutput _ControlOutput;
    private:
        // ------------ private functions-----------------//
        void GetAddonForce(const px4_command::AddonForce::ConstPtr& msg);
        void SendFleetStatus();
        void pubauxiliarystate();
        //------------- private variables ---------------//
        ros::ServiceClient   clientSendParameter;
        ros::ServiceClient   emergencyKill;
        px4_command::ControlParameter ParamSrv;
        ros::Publisher       pubAuxiliaryState;
        ros::Publisher       pubFleetStatus;
        ros::Subscriber      subAddonForce;
        /*configuration parameters*/
        bool   isPubAuxiliaryState;
        bool   isAddonForcedUsed;
        bool   isEmergency;
        int    num_drone;
        double motor_slope;
        double motor_intercept;
        float Quad_MASS;
        float Payload_Mass;
        float TotalLiftedMass;
        float Cable_Length;
        float Cable_Length_sq;
        float Cable_Tolerance;
        float MaximumInclination;
        string uav_pref;
        Eigen::Vector3f TetherOffset;
        Eigen::Matrix3f TetherOffsetCross;
        float  PayloadSharingPortion;
        px4_command:: AuxiliaryState Auxstate;
        //Controller parameter for the control law
        Eigen::Matrix3f kv;
        Eigen::Matrix3f kR;
        Eigen::Matrix3f Kphi;
        Eigen::Matrix3f kvi;
        Eigen::Matrix3f kRi;
        float kL;
        Eigen::Vector3f g_I;
        Eigen::Matrix3f lambda_j;
        Eigen::Matrix3f Ej;
        Eigen::Matrix3f D;
        // payload attitude and quadrotor relative state
        Eigen::Vector4f Quad_Drone;
        Eigen::Vector3f Delta_j;
        Eigen::Vector3f Delta_j_p;
        Eigen::Matrix3f R_Ij;
        Eigen::Matrix3f R_IP;
        Eigen::Vector3f L_j_dot;//rotation speed of the cable tip
        Eigen::Vector3f L_j; // the cable vector
        Eigen::Vector2f r_j, v_j;
        Eigen::Matrix<float, 3,2> B_j;
        Eigen::Vector3f dot_vqj;
        Eigen::Vector3f accel_body;
        Eigen::Matrix2f BB_temp;
        Eigen::Matrix2f BB_inverse;
        Eigen::Matrix3f BB_j;
        Eigen::Vector3f f_L_j;
        Eigen::Vector3f f_p_j;
        Eigen::Matrix3f Identity;
        Eigen::Vector3f Delta_pt;
        Eigen::Vector3f Delta_rt;
        Eigen::Vector2f rd_j;
        Eigen::Vector3f R1;
        Eigen::Vector3f R2;
        Eigen::Vector2f rd;
        Eigen::Vector3f f_pj;
        px4_command::AddonForce _AddonForce;
        /*
        temp variables
        */
        Eigen::Vector3d AttitudeTargetEuler;
        Eigen::Quaterniond AttitudeTargetQuaterniond;
        Eigen::Vector4f AttitudeTargetQuaternionv;
        Eigen::Vector4f AttitudeQuaternionv;
        Eigen::Matrix3f R_IPd;
        Eigen::Vector3f Xp, Vp, Xj, Vj, Omega_p, Xpd;
        /* Error signals */
        Eigen::Vector3f pos_error;
        Eigen::Vector3f angle_error;
        // error constraint parameters:
        Eigen::Vector3f pos_error_max;
        Eigen::Vector3f angular_error_max;
        Eigen::Vector3f pos_int_max;
        Eigen::Vector3f ang_int_max;
        Eigen::Vector3f fp_max;
        float tilt_max;
        float pos_int_start_error;
        float angle_int_start_error;
        // normalized control input
        Eigen::Vector3f u_l;
        Eigen::Vector3f u_d;
        Eigen::Vector3f IntegralPose;
        Eigen::Vector3f IntegralAttitude;
        Eigen::Vector3f PayloadDisturbance;
        Eigen::Vector3d thrust_sp;
        Eigen::Vector3d throttle_sp;
        float acc_x;
        float acc_y;
        float acc_z;
};

px4_command::ControlOutput payload_controller_GNC::payload_controller(
    const px4_command::DroneState& _DroneState, 
    const px4_command::TrajectoryPoint& _Reference_State, 
    float dt) 
{
    /* step 1: get command attitude and payload attitude*/
    AttitudeTargetEuler(0) = (double)_Reference_State.roll_ref;
    AttitudeTargetEuler(1) = (double)_Reference_State.pitch_ref;
    AttitudeTargetEuler(2) = (double)_Reference_State.yaw_ref;
    AttitudeTargetQuaterniond = quaternion_from_rpy(AttitudeTargetEuler);
    AttitudeTargetQuaternionv(0) = (float)AttitudeTargetQuaterniond.w();
    AttitudeTargetQuaternionv(1) = (float)AttitudeTargetQuaterniond.x();
    AttitudeTargetQuaternionv(2) = (float)AttitudeTargetQuaterniond.y();
    AttitudeTargetQuaternionv(3) = (float)AttitudeTargetQuaterniond.z();
    R_IPd =  QuaterionToRotationMatrix(AttitudeTargetQuaternionv);
    // get the payload rotation matrix
    for(int i = 0; i < 4 ;i++){
        AttitudeQuaternionv(i) = _DroneState.payload_quaternion[i];
    }

    R_IP = QuaterionToRotationMatrix(AttitudeQuaternionv);
    // get the quadrotor rotation matrix
    Quad_Drone(0) = _DroneState.attitude_q.w;
    Quad_Drone(1) = _DroneState.attitude_q.x;
    Quad_Drone(2) = _DroneState.attitude_q.y;
    Quad_Drone(3) = _DroneState.attitude_q.z;
    R_Ij =  QuaterionToRotationMatrix(Quad_Drone);
    // get the acceleration reading from IMU
    for( int i = 0; i < 3 ; i ++) {
        accel_body(i) = _DroneState.acceleration[i];
    }
    dot_vqj = R_Ij * (accel_body)+ g_I;// calculate true acc in inertial frame dot_vqj in TCST paper
    /*Step 2 calculate L_j and L_j_dot based on payload information feedback  */
    // put states into vector array for easy calculations
    for (int i = 0; i < 3; i ++) {
        Xpd(i) = _Reference_State.position_ref[i];
        Xj(i) = _DroneState.position[i];
        Xp(i) = _DroneState.payload_pos[i];
        Vp(i) = _DroneState.payload_vel[i];
        Vj(i) = _DroneState.velocity[i];
        Omega_p(i) = _DroneState.payload_angular_vel[i];
    }
    L_j = Xj - Xp - R_IP * TetherOffset;
    L_j_dot = Vj- Vp + R_IP * TetherOffsetCross* Omega_p;

    r_j(0) = L_j(0);
    r_j(1) = L_j(1);
    v_j(0) = L_j_dot(0);
    v_j(1) = L_j_dot(1);

    acc_x = _DroneState.acceleration[0];
    acc_y = _DroneState.acceleration[1];
    acc_z = _DroneState.acceleration[2];

    float sq_r = r_j(0)*r_j(0) + r_j(1)*r_j(1);

    if (Cable_Length_sq - sq_r>0.01)
    {
        B_j(2,0) =  -r_j(0)/sqrt((Cable_Length_sq - sq_r));
        B_j(2,1) =  -r_j(1)/sqrt((Cable_Length_sq - sq_r));
    }else{
        B_j(2,0) = -0.1;
        B_j(2,1) = -0.1;
    }

    BB_temp = B_j.transpose()*B_j; 
    float BB_determinent = BB_temp(0,0)*BB_temp(1,1) - BB_temp(0,1)*BB_temp(1,0);
    BB_inverse(0,0) = BB_temp(1,1);
    BB_inverse(1,1) = BB_temp(0,0);
    BB_inverse(0,1) = - BB_temp(0,1);
    BB_inverse(1,0) = - BB_temp(1,0);
    BB_inverse = BB_inverse/BB_determinent;
    BB_j = B_j * BB_inverse * B_j.transpose();

    /*Step 3 calculate payload position and attitude error*/
    pos_error = Xp - Xpd;
    float scale_p = sqrt(1 + pos_error.transpose() * pos_error);
    pos_error = pos_error/scale_p;

    angle_error = 0.5* Veemap(R_IPd.transpose()*R_IP- R_IP.transpose() * R_IPd);
    if (num_drone<3) // if only two drones are involved, we have to remove one axis in control
    {
         angle_error(0) = 0;
    }
     
    /*Step 4 calculate control law form the GNC 2019 paper*/
    for (int i=0; i<3; i++) {
        if(abs(IntegralAttitude(i)) < angle_int_start_error ) {
            IntegralAttitude(i) += angle_error(i) *dt;
            if(abs(IntegralAttitude(i) > ang_int_max[i]))
            {
                cout << "Angle Integral Saturation! " << " [0-1-2] "<< i <<endl;
                cout << "[integral]: "<< IntegralAttitude(i)<<" [int_max]: "<<ang_int_max[i]<<" [] "<<endl;
            }

            IntegralAttitude(i) = constrain_function(IntegralAttitude(i), ang_int_max[i]);

        }else {
            IntegralAttitude(i) = 0;
        }

        if(abs(pos_error[i]) < pos_int_start_error) {
           
            IntegralPose(i) += pos_error(i) * dt;

            if(abs(IntegralPose(i) > pos_int_max[i]))
            {
                cout << "Pose Integral saturation! " << " [0-1-2] "<< i <<endl;
                cout << "[integral]: "<< IntegralPose(i)<<" [int_max]: "<< pos_int_max[i] <<" [m] "<<endl;
            }
            IntegralPose(i) = constrain_function(IntegralPose(i), pos_int_max[i]);
        }else {
            IntegralPose(i) = 0;
        }
        // If not in OFFBOARD mode, set all intergral to zero.
        if(_DroneState.mode != "OFFBOARD") {
            IntegralPose(i) = 0;
            IntegralAttitude(i) = 0;
        }
    }
    // calculate disturbance force on the quadrotor
    if(_DroneState.mode != "OFFBOARD") {
        Delta_j.setZero(); // estimation does not run in offboard mode
    } else {
        Delta_j +=  lambda_j * dt * BB_j * (Quad_MASS * (dot_vqj - g_I) - f_L_j - Delta_j);
    }
    Delta_j_p = (Identity - L_j*L_j.transpose()/Cable_Length_sq)*Delta_j; // get the effective disturbance
    Delta_j_p =  constrain_vector(Delta_j_p, 10.0);// constraint the disturbance estimation.
    // calculate the total compensation force:
    f_p_j = - PayloadSharingPortion * (Payload_Mass*g_I + Delta_pt + R_IP * Ej * Delta_rt);
    rd_j = Cable_Length *  f_p_j.segment<2>(0)/f_p_j.norm(); // calculate the desired cable tile angle
    Eigen::Vector3f U;
    if (isAddonForcedUsed) {
        PayloadDisturbance =  PayloadSharingPortion * ( Delta_pt + R_IP * Ej * Delta_rt);
        U = Vp + kv*(pos_error + kvi * IntegralPose) 
            - R_IP * TetherOffsetCross*(Omega_p + kR * angle_error + kRi * IntegralAttitude) 
            + B_j*(v_j + kL*(r_j - rd_j)); // rd_j is used when the total estimation is used
    } else {
        PayloadDisturbance.setZero();//if not used, set this compensation force to 0.
        U = Vp + kv*(pos_error + kvi * IntegralPose) 
            - R_IP * TetherOffsetCross*(Omega_p + kR * angle_error + kRi * IntegralAttitude) 
            + B_j*(v_j + kL*r_j); // rd_j is not used if addon force is not used
    }
    //u_l = - Kphi * (Vj+kv*(pos_error + kvi * IntegralPose)- R_IP * TetherOffsetCross *(kR * angle_error + kRi * IntegralAttitude) + kL*B_j*r_j);
    u_l = - Kphi * U - (Delta_j_p + PayloadDisturbance)/(TotalLiftedMass);
    // desired acceleration
    accel_sp[0] = u_l[0] - u_d[0];
    accel_sp[1] = u_l[1] - u_d[1];
    accel_sp[2] = u_l[2] - u_d[2] + 9.81;
    f_L_j = TotalLiftedMass * accel_sp.cast <float> ();
    // calculate the required thrust under tilt angle constraint
    thrust_sp =  px4_command_utils::accelToThrust(accel_sp, TotalLiftedMass, tilt_max);
    // calculate the required throttle command
    throttle_sp = px4_command_utils::thrustToThrottleLinear(thrust_sp, motor_slope, motor_intercept);

    if (isPubAuxiliaryState) {
        pubauxiliarystate();// send the auxiliary status for analysis
    }

    SendFleetStatus();// send fleet status for robust control
    // finally, send the control output to FCU
    for (int i=0; i<3; i++)
    {
        _ControlOutput.u_l[i] = u_l[i];
        _ControlOutput.u_d[i] = u_d[i];
        _ControlOutput.Thrust[i]   = thrust_sp[i];
        _ControlOutput.Throttle[i] = throttle_sp[i];
    }
    return _ControlOutput;

}

void payload_controller_GNC::pubauxiliarystate() 
{
        // record time
        Auxstate.header.stamp = ros::Time::now();
        Auxstate.IntegralPose_x = IntegralPose(0);
        Auxstate.IntegralPose_y = IntegralPose(1);
        Auxstate.IntegralPose_z = IntegralPose(2);

        Auxstate.IntegralAngle_x = IntegralAttitude(0);
        Auxstate.IntegralAngle_y = IntegralAttitude(1);
        Auxstate.IntegralAngle_z = IntegralAttitude(2);

        Auxstate.q_0 = Quad_Drone(0);
        Auxstate.q_1 = Quad_Drone(1);
        Auxstate.q_2 = Quad_Drone(2);
        Auxstate.q_3 = Quad_Drone(3);

        Auxstate.r_jx = r_j(0);
        Auxstate.r_jy = r_j(1);

        Auxstate.v_jx = v_j(0);
        Auxstate.v_jy = v_j(1);

        Auxstate.pos_error_x = pos_error(0);
        Auxstate.pos_error_y = pos_error(1);
        Auxstate.pos_error_z = pos_error(2);

        Auxstate.angle_error_x =  angle_error(0);
        Auxstate.angle_error_y =  angle_error(1);
        Auxstate.angle_error_z =  angle_error(2);
       
        Eigen::Vector3d Euler = quaternion_to_euler2(AttitudeQuaternionv);
        Auxstate.Euler_roll =   Euler(0)*57.3;
        Auxstate.Euler_pitch =  Euler(1)*57.3;
        Auxstate.Euler_yaw  =   Euler(2)*57.3;

        Auxstate.u_lx  = u_l(0);
        Auxstate.u_ly  = u_l(1);
        Auxstate.u_lz  = u_l(2);

        Auxstate.acc_x = acc_x;
        Auxstate.acc_y = acc_y;
        Auxstate.acc_z = acc_z;

        Auxstate.rd_jx = rd_j(0);
        Auxstate.rd_jy = rd_j(1);

        pubAuxiliaryState.publish(Auxstate);
}

void payload_controller_GNC::SendFleetStatus()
{
    px4_command::FleetStatus FleetStatus_;
    FleetStatus_.header.stamp = ros::Time::now();
    FleetStatus_.r_jx = r_j(0);
    FleetStatus_.r_jy = r_j(1);
    FleetStatus_.v_jx = v_j(0);  
    FleetStatus_.v_jy = v_j(1);

    FleetStatus_.f_Ljx = 4*thrust_sp(0);
    FleetStatus_.f_Ljy = 4*thrust_sp(1);       
    FleetStatus_.f_Ljz = 4*thrust_sp(2);       

    FleetStatus_.delta_jx = Delta_j_p(0);
    FleetStatus_.delta_jy = Delta_j_p(1);   
    FleetStatus_.delta_jz = Delta_j_p(2);   
    FleetStatus_.emergency = isEmergency;
    pubFleetStatus.publish(FleetStatus_);
}

void payload_controller_GNC::GetAddonForce(const px4_command::AddonForce::ConstPtr& msg)  
{
    _AddonForce = *msg;
    Delta_pt(0) = _AddonForce.delta_Tx;
    Delta_pt(1) = _AddonForce.delta_Ty;
    Delta_pt(2) = _AddonForce.delta_Tz;
    Delta_rt(0) = _AddonForce.delta_Rx;
    Delta_rt(1) = _AddonForce.delta_Ry;
    Delta_rt(2) = _AddonForce.delta_Rz;
    R1(0) = _AddonForce.R_1x;
    R1(1) = _AddonForce.R_1y;
    R1(2) = _AddonForce.R_1z;
    R2(0) = _AddonForce.R_2x;
    R2(1) = _AddonForce.R_2y;  
    R2(2) = _AddonForce.R_2z; 
}

bool payload_controller_GNC::emergency_switch()
{
    
    /* 1. check whether the total length of the string is within a safe range */
    if (L_j.norm()>Cable_Length*Cable_Tolerance)
    {
        isEmergency = true;
    }
    /* 2. check if the r_j is too large */

    if (r_j.norm()>Cable_Length*sin(MaximumInclination/57.3))
    {
        isEmergency = true;
    }

    return isEmergency;   
}

void payload_controller_GNC::printf_result()
{
    cout <<">>>>>>>>  TCST 2019 Paylaod Pose Controller "<< uav_pref <<" <<<<<<<" <<endl;

    //固定的浮点显示
    cout.setf(ios::fixed);
    //左对齐
    cout.setf(ios::left);
    // 强制显示小数点
    cout.setf(ios::showpoint);
    // 强制显示符号
    cout.setf(ios::showpos);

    cout<<setprecision(3);

    if (!isEmergency) {
        cout << ">>>>>>  Payload Measurements and Errors <<<<<<<<<" <<endl;
        // priint out the control status.
        cout << "u_l [X Y Z] : " << u_l[0] << " [m/s^2] "<< u_l[1]<<" [m/s^2] "<<u_l[2]<<" [m/s^2] "<<endl;
        cout << "u_d [X Y Z] : " << u_d[0] << " [N] "<< u_d[1]<<" [N] "<<u_d[2]<<" [N] "<<endl;
        cout << "IntegralPose [X Y Z] : " << IntegralPose(0) << " [N] " << IntegralPose(1) << " [N] " << IntegralPose(2) <<" [N] "<<endl;
        cout << "r_j [X Y] : " << r_j(0) << " [m] " << r_j(1) << " [m] " <<endl;
        cout << "v_j [X Y] : " << v_j(0) << " [m/s] "<< v_j(1) << " [m/s] " << endl;
        cout << "Pos Error [X Y Z] : " << pos_error[0] << " [m] " <<  pos_error[1] << " [m] " <<  pos_error[2] << " [m] " << endl;
        cout << "Angle Error [X Y Z] : " << angle_error[0] << " [] "<< angle_error[1] << " [] " << angle_error[2] << " [] " <<endl;
        cout << ">>>>>> Payload Target Attitude and Attitude Verification <<<<<<<<<" <<endl;
        // verify the target quaternion has been calculated:
        Eigen::Vector3d Euler_Target = quaternion_to_euler2(AttitudeTargetQuaternionv);
        cout << "Target Euler, roll : "  << Euler_Target(0)*57.3 << " [DEG] ";
        cout << "pitch : " << Euler_Target(1)*57.3 << " [DEG] ";
        cout << "yaw : "   << Euler_Target(2)*57.3 << " [DEG] ";
        cout << endl;
        // display rotation matrix:
        cout << "R_IP: " <<endl;
        cout << R_IP<<endl;
        cout << "R_IPd: " <<endl;
        cout << R_IPd<<endl;
        // verify that the payload attitude
        Eigen::Vector3d Euler = quaternion_to_euler2(AttitudeQuaternionv);
        cout << "Current Euler, roll : "  << Euler(0)*57.3 << " [DEG] ";
        cout << "pitch : " << Euler(1)*57.3 << " [DEG] ";
        cout << "yaw : "   << Euler(2)*57.3 << " [DEG] ";
        cout << endl;
        cout << ">>>>>> disturbance estimation <<<<<<<<<" <<endl;
        // display estimation force on quadrotor
        cout << "Delta_jp x: " << Delta_j_p (0)<< " [N] ";
        cout << "Delta_jp y: " << Delta_j_p (1)<< " [N] ";
        cout << "Delta_jp z: " << Delta_j_p (2)<< " [N] ";
        cout << endl;
        /* display addon on force*/
        cout << "Delta_pt x: "<< Delta_pt(0) << " [N] ";
        cout << "Delta_pt y: "<< Delta_pt(1) << " [N] ";
        cout << "Delta_pt z: "<< Delta_pt(2) << " [N] ";
        cout << endl;
        cout << "Delta_rt x: "<< Delta_rt(0) << " [N] ";
        cout << "Delta_rt y: "<< Delta_rt(1) << " [N] ";
        cout << "Delta_rt z: "<< Delta_rt(2) << " [N] ";
        cout << endl;   
        /* display the total compensation force for payload*/ 
        cout << "f_p_j [X Y Z]: " << f_p_j(0)<< " [N] " <<  f_p_j(1)<< " [N] " << f_p_j(2)<< " [N] " <<endl;
        /* display the desired cable inclination */
        cout << "rd_j : " << rd_j(0) << " [m] " << rd_j(1) << " [m] " <<endl;
        cout << ">>>>>> Control Ouput <<<<<<<<<" <<endl;
        // display total control force and throttle
        cout << "F_L_" << uav_pref <<  " [X Y Z] : " << 4*thrust_sp(0) <<" [N] "<<4*thrust_sp(1) <<" [N] "<< 4*thrust_sp(2) <<" [N] "<<endl;
        cout << "Throttle Setpoint "<<  uav_pref <<  " [X Y Z] : " << throttle_sp(0) <<" [] " << throttle_sp(1) << " [] " << throttle_sp(2) << " [] " <<endl;
    } else {
        cout << "+++++++++++++++++++++++++++++++++++++++++++++" <<endl;
        ROS_WARN(">>>>>>> Danger, Switch to MoveENU !!<<<<<<<<<");
        cout << "+++++++++++++++++++++++++++++++++++++++++++++" <<endl;
    }
}

// print out controller parameters
void payload_controller_GNC::printf_param()
{    
    cout <<">>>>>>>> Parameter For Payload Stabilization Controller (TCST 2019) <<<<<<<<<" <<endl;
    cout <<"System Parameter:  " <<endl;
    cout <<"UAV ID : "<<uav_pref<<endl;
    cout <<"Quad_MASS : "<< Quad_MASS << " [kg] " << endl;
    cout <<"Payload_MASS : "<< Payload_Mass << " [kg] " << endl;
    cout <<"TotalLiftedMass" << TotalLiftedMass << " [kg] "<<endl;
    cout <<"Cable_Length : "<< Cable_Length << " [m] "  <<  endl;
    cout <<"Num of Drones: " << num_drone <<endl;
    cout <<"Tether Offset x : "<< TetherOffset(0) << " [m] ";
    cout <<"Tether Offset y : "<< TetherOffset(1) << " [m] ";
    cout <<"Tether Offset z : "<< TetherOffset(2) << " [m] ";
    cout << endl;
    cout << "D: " <<D <<endl;
    cout << "Ej: " <<Ej <<endl;
    cout <<"a_j : " <<PayloadSharingPortion<<endl;
    cout << "Motor Parameters: " << endl;
    cout << "Motor Curve Slop: " << motor_slope << " Motor Curve Intercept: "<<  motor_intercept <<endl;
    cout <<"Basic Control Gains:  " <<endl;
    cout <<"kv_x : "<< kv(0,0) << endl;
    cout <<"kv_y : "<< kv(1,1) << endl;
    cout <<"kv_z : "<< kv(2,2) << endl;
    cout <<"kR_x : "<< kR(0,0)<<endl;
    cout <<"kR_y : "<< kR(1,1)<<endl;
    cout <<"kR_z : "<< kR(2,2)<<endl;  

    cout << "Advanced Cross-Feeding Gains: " << endl;
  
    cout << " Integral Gains (0 if robust controller is used) " << endl;
    cout <<"kvi_x : "<< kvi(0,0) << endl;
    cout <<"kvi_y : "<< kvi(1,1) << endl;
    cout <<"kvi_z : "<< kvi(2,2) << endl;
    cout <<"kphi_x : " << Kphi(0,0) <<endl;
    cout <<"kphi_y : " << Kphi(1,1) <<endl;
    cout <<"kphi_z : " << Kphi(2,2) <<endl;
    // Display estimation parameter
    cout <<"UDE parameter:  " << endl;
    cout << "lambda_jx: " <<  lambda_j(0,0) << " lambda_jy: " << lambda_j(1,1) << " lambda_jz: "<< lambda_j(2,2) << endl;
    // Display control limitation:
    cout <<"Control Limit:  " <<endl;
    cout <<"pxy_error_max : "<< pos_error_max[0] << endl;
    cout <<"pz_error_max :  "<< pos_error_max[2] << endl;
    cout <<"maximum tilt angle: "<< tilt_max << " [DEG] " << endl;
    cout << "fpmax_x : " << fp_max(0) << " fpmax_y : " << fp_max(1) << " fpmax_z : " << fp_max(2) <<endl;

}

#endif
