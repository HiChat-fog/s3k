/*
 * quad_core.c - pure C translation of repos/quad_pid dynamics + PID.
 * Mapping guide (Python -> C):
 *   Quadcopter.py   -> qdrone_t + quad_*()
 *   PID_Controller  -> pid3_t + pid_update()
 *   Quadcopter_main -> quad_control_step()
 */
#include "quad_core.h"

#include <math.h>

void quad_init(qdrone_t *q, const float pos[3], const float vel[3],
	       const float ang[3], const float ang_vel[3], float dt)
{
	int i;
	for (i = 0; i < 3; i++) {
		q->pos[i] = pos[i];
		q->vel[i] = vel[i];
		q->angle[i] = ang[i];
		q->ang_vel[i] = ang_vel[i];
		q->lin_acc[i] = 0.f;
		q->ang_acc[i] = 0.f;
		q->vel_ref[i] = 0.f;
		q->angle_ref[i] = 0.f;
		q->ang_vel_ref[i] = 0.f;
		q->tau[i] = 0.f;
	}
	q->pos_ref[0] = 0.f;
	q->pos_ref[1] = 0.f;
	q->pos_ref[2] = 3.f; /* hover at z=3 m, as in Quadcopter_main */
	for (i = 0; i < 4; i++)
		q->speeds[i] = 1.f;

	q->time = 0.f;
	q->dt = dt;
	q->mass = 0.506f;
	q->gravity = 9.8f;
	q->Ixx = 8.11858e-5f;
	q->Iyy = 8.11858e-5f;
	q->Izz = 6.12233e-5f;
	q->L = 0.2f;
	q->kt = 1e-7f;
	q->b_prop = 1e-9f;
	q->Cd = 1.f;
	q->density = 1.225f;
	q->A_ref = 0.02f;
	q->max_angle = (float)(M_PI / 12.0);
	q->thrust = q->mass * q->gravity;
}

void pid_init(pid3_t *p, const float Kp[3], const float Ki[3],
	      const float Kd[3], const float Ki_sat[3], float dt)
{
	int i;
	for (i = 0; i < 3; i++) {
		p->Kp[i] = Kp[i];
		p->Ki[i] = Ki[i];
		p->Kd[i] = Kd[i];
		p->Ki_sat[i] = Ki_sat[i];
		p->integ[i] = 0.f;
	}
	p->dt = dt;
}

void pid_update(pid3_t *p, const float err[3], const float derr[3],
		float out[3])
{
	int i;
	for (i = 0; i < 3; i++) {
		p->integ[i] += err[i] * p->dt;
		/* anti-windup: clamp integral to Ki_sat */
		float mag = fabsf(p->integ[i]);
		if (mag > p->Ki_sat[i])
			p->integ[i] = (p->integ[i] / mag) * p->Ki_sat[i];
		out[i] = p->Kp[i] * err[i] + p->Ki[i] * p->integ[i] +
			 p->Kd[i] * derr[i];
	}
}

/* body-to-inertial rotation matrix R (angle = [phi,theta,psi]) */
static void body2inertial(const qdrone_t *q, float R[3][3])
{
	float c1 = cosf(q->angle[0]), s1 = sinf(q->angle[0]);
	float c2 = cosf(q->angle[1]), s2 = sinf(q->angle[1]);
	float c3 = cosf(q->angle[2]), s3 = sinf(q->angle[2]);
	R[0][0] = c2 * c3;
	R[0][1] = c3 * s1 * s2 - c1 * s3;
	R[0][2] = s1 * s3 + c1 * s2 * c3;
	R[1][0] = c2 * s3;
	R[1][1] = c1 * c3 + s1 * s2 * s3;
	R[1][2] = c1 * s3 * s2 - c3 * s1;
	R[2][0] = -s2;
	R[2][1] = c2 * s1;
	R[2][2] = c1 * c2;
}

static void thetadot2omega(const qdrone_t *q, const float ang_vel[3],
			   float omega[3])
{
	float c1 = cosf(q->angle[0]), s1 = sinf(q->angle[0]);
	float c2 = cosf(q->angle[1]), s2 = sinf(q->angle[1]);
	/* R = [[1,0,-s2],[0,c1,c2*s1],[0,-s1,c2*c1]] */
	omega[0] = ang_vel[0] - s2 * ang_vel[2];
	omega[1] = c1 * ang_vel[1] + c2 * s1 * ang_vel[2];
	omega[2] = -s1 * ang_vel[1] + c2 * c1 * ang_vel[2];
}

static void find_omegadot(const qdrone_t *q, const float omega[3],
			  float omega_dot[3])
{
	/* I @ omega then cross(omega, I*omega), scale by inv(I) (diagonal) */
	float Iomega[3], cross[3];
	Iomega[0] = q->Ixx * omega[0];
	Iomega[1] = q->Iyy * omega[1];
	Iomega[2] = q->Izz * omega[2];
	cross[0] = omega[1] * Iomega[2] - omega[2] * Iomega[1];
	cross[1] = omega[2] * Iomega[0] - omega[0] * Iomega[2];
	cross[2] = omega[0] * Iomega[1] - omega[1] * Iomega[0];
	omega_dot[0] = (q->tau[0] - cross[0]) / q->Ixx;
	omega_dot[1] = (q->tau[1] - cross[1]) / q->Iyy;
	omega_dot[2] = (q->tau[2] - cross[2]) / q->Izz;
}

static void omegadot2Edot(const qdrone_t *q, const float omega_dot[3],
			  float E_dot[3])
{
	float c1 = cosf(q->angle[0]), s1 = sinf(q->angle[0]);
	float c2 = cosf(q->angle[1]);
	float t2 = tanf(q->angle[1]);
	E_dot[0] = omega_dot[0] + s1 * t2 * omega_dot[1] +
		   c1 * t2 * omega_dot[2];
	E_dot[1] = c1 * omega_dot[1] - s1 * omega_dot[2];
	E_dot[2] = (s1 / c2) * omega_dot[1] + (c1 / c2) * omega_dot[2];
}

static void find_lin_acc(qdrone_t *q)
{
	float R[3][3], RI[3][3];
	float thrust_i[3], drag_b[3], drag_i[3], weight[3], acc[3];
	float v[3];
	int i, j;

	body2inertial(q, R);
	for (i = 0; i < 3; i++)
		for (j = 0; j < 3; j++)
			RI[i][j] = R[j][i];

	/* thrust in body frame -> inertial */
	thrust_i[0] = R[0][2] * q->thrust;
	thrust_i[1] = R[1][2] * q->thrust;
	thrust_i[2] = R[2][2] * q->thrust;

	/* velocity in body frame -> drag (element-wise square) */
	for (i = 0; i < 3; i++) {
		v[i] = RI[i][0] * q->vel[0] + RI[i][1] * q->vel[1] +
		       RI[i][2] * q->vel[2];
		drag_b[i] = -q->Cd * 0.5f * q->density * q->A_ref * v[i] * v[i];
	}
	for (i = 0; i < 3; i++)
		drag_i[i] = R[i][0] * drag_b[0] + R[i][1] * drag_b[1] +
			    R[i][2] * drag_b[2];

	weight[0] = 0.f;
	weight[1] = 0.f;
	weight[2] = -q->mass * q->gravity;

	for (i = 0; i < 3; i++) {
		acc[i] = (thrust_i[i] + drag_i[i] + weight[i]) / q->mass;
		q->lin_acc[i] = acc[i];
	}
}

void quad_des2speeds(qdrone_t *q, float thrust_des, const float tau_des[3])
{
	int i;
	float e1 = tau_des[0] * q->Ixx;
	float e2 = tau_des[1] * q->Iyy;
	float e3 = tau_des[2] * q->Izz;
	int n = 4;
	float ws = thrust_des / ((float)n * q->kt);
	float s[4];

	s[0] = ws - e2 / (((float)n / 2.f) * q->kt * q->L) -
	       e3 / ((float)n * q->b_prop);
	s[1] = ws - e1 / (((float)n / 2.f) * q->kt * q->L) +
	       e3 / ((float)n * q->b_prop);
	s[2] = ws + e2 / (((float)n / 2.f) * q->kt * q->L) -
	       e3 / ((float)n * q->b_prop);
	s[3] = ws + e1 / (((float)n / 2.f) * q->kt * q->L) +
	       e3 / ((float)n * q->b_prop);

	/* clamp per-motor thrust to [minT, maxT] */
	for (i = 0; i < 4; i++) {
		float t = s[i] * q->kt;
		if (t > 16.5f)
			s[i] = 16.5f / q->kt;
		else if (t < 0.5f)
			s[i] = 0.5f / q->kt;
		q->speeds[i] = s[i];
	}
}

static void find_body_torque(qdrone_t *q)
{
	q->tau[0] = q->L * q->kt * (q->speeds[3] - q->speeds[1]);
	q->tau[1] = q->L * q->kt * (q->speeds[2] - q->speeds[0]);
	q->tau[2] = q->b_prop * (-q->speeds[0] + q->speeds[1] -
				 q->speeds[2] + q->speeds[3]);
}

void quad_step(qdrone_t *q)
{
	float omega[3], omega_dot[3], E_dot[3];
	int i;

	q->thrust = q->kt * (q->speeds[0] + q->speeds[1] + q->speeds[2] +
			     q->speeds[3]);
	find_lin_acc(q);
	find_body_torque(q);
	thetadot2omega(q, q->ang_vel, omega);
	find_omegadot(q, omega, omega_dot);
	omegadot2Edot(q, omega_dot, E_dot);
	for (i = 0; i < 3; i++) {
		q->ang_acc[i] = E_dot[i];
		q->ang_vel[i] += q->dt * q->ang_acc[i];
		q->angle[i] += q->dt * q->ang_vel[i];
		q->vel[i] += q->dt * q->lin_acc[i];
		q->pos[i] += q->dt * q->vel[i];
	}
	q->time += q->dt;
}

/* One full control tick, mirroring Quadcopter_main.py's per-step body. */
void quad_control_step(qdrone_t *q, pid3_t *pos_p, pid3_t *ang_p)
{
	float pos_err[3], vel_err[3], des_acc[3], tau_needed[3];
	float ang_err[3], ang_vel_err[3];
	float mag_acc, thrust_needed, mag_ang;
	float ang_des[3];
	int i;

	for (i = 0; i < 3; i++) {
		pos_err[i] = q->pos_ref[i] - q->pos[i];
		vel_err[i] = q->vel_ref[i] - q->vel[i];
	}
	pid_update(pos_p, pos_err, vel_err, des_acc);

	/* z-gain includes thrust needed to hover */
	des_acc[2] = (q->gravity + des_acc[2]) /
		     (cosf(q->angle[0]) * cosf(q->angle[1]));
	thrust_needed = q->mass * des_acc[2];

	mag_acc = sqrtf(des_acc[0] * des_acc[0] + des_acc[1] * des_acc[1] +
			des_acc[2] * des_acc[2]);
	if (mag_acc < 1e-9f)
		mag_acc = 1.f;

	/* desired attitude from desired acceleration (tilt-to-move) */
	ang_des[0] = asinf(-des_acc[1] / mag_acc / cosf(q->angle[1]));
	ang_des[1] = asinf(des_acc[0] / mag_acc);
	ang_des[2] = 0.f;
	mag_ang = sqrtf(ang_des[0] * ang_des[0] + ang_des[1] * ang_des[1] +
			ang_des[2] * ang_des[2]);
	if (mag_ang > q->max_angle) {
		float s = q->max_angle / mag_ang;
		ang_des[0] *= s;
		ang_des[1] *= s;
		ang_des[2] *= s;
	}

	q->angle_ref[0] = ang_des[0];
	q->angle_ref[1] = ang_des[1];
	q->angle_ref[2] = ang_des[2];
	for (i = 0; i < 3; i++) {
		ang_err[i] = q->angle_ref[i] - q->angle[i];
		ang_vel_err[i] = q->ang_vel_ref[i] - q->ang_vel[i];
	}
	pid_update(ang_p, ang_err, ang_vel_err, tau_needed);

	quad_des2speeds(q, thrust_needed, tau_needed);
	quad_step(q);
}