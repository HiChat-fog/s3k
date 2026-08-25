/*
 * quad_core.h - Quadcopter dynamics + PID, pure C translation of
 * repos/quad_pid (Quadcopter.py + PID_Controller.py).
 *
 * No S3K dependency: compiles standalone so the dynamics can be verified
 * on the host before being driven by the S3K attitude loop.
 */
#ifndef QUAD_CORE_H
#define QUAD_CORE_H

typedef struct {
	float pos[3];    /* inertial position [x,y,z], m */
	float vel[3];    /* inertial velocity, m/s */
	float angle[3];  /* Euler angles [phi,theta,psi], rad */
	float ang_vel[3];/* Euler angle rates, rad/s */
	float lin_acc[3];
	float ang_acc[3];
	float pos_ref[3];
	float vel_ref[3];
	float angle_ref[3];
	float ang_vel_ref[3];
	float speeds[4]; /* motor speeds, rpm */
	float tau[3];    /* body torque, N*m */
	float thrust;    /* total thrust, N */
	float time;
	float dt;
	/* vehicle constants */
	float mass, gravity;
	float Ixx, Iyy, Izz;
	float L, kt, b_prop;
	float Cd, density, A_ref;
	float max_angle;
} qdrone_t;

typedef struct {
	float Kp[3], Ki[3], Kd[3], Ki_sat[3];
	float integ[3];
	float dt;
} pid3_t; /* 3-channel PID (roll/pitch/yaw or x/y/z) */

void quad_init(qdrone_t *q, const float pos[3], const float vel[3],
	       const float ang[3], const float ang_vel[3], float dt);
void pid_init(pid3_t *p, const float Kp[3], const float Ki[3],
	      const float Kd[3], const float Ki_sat[3], float dt);
/* out = Kp*err + Ki*integ + Kd*derr, with anti-windup on integ */
void pid_update(pid3_t *p, const float err[3], const float derr[3],
		float out[3]);
void quad_des2speeds(qdrone_t *q, float thrust_des, const float tau_des[3]);
void quad_step(qdrone_t *q);

/* convenience: one full attitude-control step (used by the S3K PID2 loop) */
void quad_control_step(qdrone_t *q, pid3_t *pos_p, pid3_t *ang_p);

#endif /* QUAD_CORE_H */