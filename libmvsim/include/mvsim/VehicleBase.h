/*+-------------------------------------------------------------------------+
  |                       MultiVehicle simulator (libmvsim)                 |
  |                                                                         |
  | Copyright (C) 2014-2020  Jose Luis Blanco Claraco                       |
  | Copyright (C) 2017  Borys Tymchenko (Odessa Polytechnic University)     |
  | Distributed under 3-clause BSD License                                  |
  |   See COPYING                                                           |
  +-------------------------------------------------------------------------+ */

#pragma once

#include <Box2D/Collision/Shapes/b2PolygonShape.h>
#include <Box2D/Dynamics/b2Body.h>
#include <Box2D/Dynamics/b2Fixture.h>
#include <Box2D/Dynamics/b2World.h>
#include <mrpt/img/TColor.h>
#include <mrpt/opengl/CSetOfLines.h>
#include <mrpt/opengl/CSetOfObjects.h>
#include <mrpt/poses/CPose2D.h>
#include <mvsim/ClassFactory.h>
#include <mvsim/ControllerBase.h>
#include <mvsim/FrictionModels/FrictionBase.h>
#include <mvsim/Sensors/SensorBase.h>
#include <mvsim/Simulable.h>
#include <mvsim/VisualObject.h>
#include <mvsim/Wheel.h>
#include <mvsim/basic_types.h>

#include <map>
#include <mutex>
#include <string>

#include "CsvLogger.h"

namespace mvsim
{
/** Virtual base class for each vehicle "actor" in the simulation.
 * Derived classes implements different dynamical models (Differential,
 * Ackermann,...)
 */
class VehicleBase : public VisualObject, public Simulable
{
   public:
	/** Class factory: Creates a vehicle from XML description of type
	 * "<vehicle>...</vehicle>".  */
	static VehicleBase* factory(
		World* parent, const rapidxml::xml_node<char>* xml_node);
	/// \overload
	static VehicleBase* factory(World* parent, const std::string& xml_text);

	/** Register a new class of vehicles from XML description of type
	 * "<vehicle:class name='name'>...</vehicle:class>".  */
	static void register_vehicle_class(
		const rapidxml::xml_node<char>* xml_node);

	// ------- Interface with "World" ------
	virtual void simul_pre_timestep(const TSimulContext& context) override;
	virtual void simul_post_timestep(const TSimulContext& context) override;
	virtual void apply_force(
		double fx, double fy, double local_ptx = 0.0,
		double local_pty = 0.0) override;

	/** Gets the body dynamical state into q, dot{q} */
	void simul_post_timestep_common(const TSimulContext& context);

	/** Create bodies, fixtures, etc. for the dynamical simulation. May be
	 * overrided by derived classes */
	virtual void create_multibody_system(b2World& world);

	/** Get (an approximation of) the max radius of the vehicle, from its point
	 * of reference (in meters) */
	virtual float getMaxVehicleRadius() const { return m_max_radius; }
	/** Get the overall vehicle mass, excluding wheels. */
	virtual double getChassisMass() const { return m_chassis_mass; }
	b2Body* getBox2DChassisBody() { return m_b2d_vehicle_body; }
	mrpt::math::TPoint2D getChassisCenterOfMass() const
	{
		return m_chassis_com;
	}  //!< In local coordinates (this excludes the mass of wheels)

	size_t getNumWheels() const { return m_wheels_info.size(); }
	const Wheel& getWheelInfo(const size_t idx) const
	{
		return m_wheels_info[idx];
	}
	Wheel& getWheelInfo(const size_t idx) { return m_wheels_info[idx]; }
	const mrpt::math::TPose3D& getPose() const
	{
		return m_q;
	}  //!< Last time-step pose (of the ref. point, in global coords)
	   //!(ground-truth)
	void setPose(const mrpt::math::TPose3D& p) const
	{
		const_cast<mrpt::math::TPose3D&>(m_q) = p;
	}  //!< Manually override vehicle pose (Use with caution!) (purposely set as
	   //!"const")

	mrpt::poses::CPose2D getCPose2D() const;  //!< \overload
	/** Last time-step velocity (of the ref. point, in global coords)
	 * (ground-truth) */
	const vec3& getVelocity() const { return m_dq; }
	/** Last time-step velocity (of the ref. point, in local coords)
	 * (ground-truth)
	 * \sa getVelocityLocalOdoEstimate() */
	vec3 getVelocityLocal() const;
	/** Current velocity of each wheel's center point (in local coords). Call
	 * with veh_vel_local=getVelocityLocal() for ground-truth.  */
	void getWheelsVelocityLocal(
		std::vector<mrpt::math::TPoint2D>& vels,
		const vec3& veh_vel_local) const;

	/** Gets the current estimation of odometry-based velocity as reconstructed
	 * solely from wheels spinning velocities and geometry.
	 * This is the input of any realistic low-level controller onboard.
	 * \sa getVelocityLocal() */
	virtual vec3 getVelocityLocalOdoEstimate() const = 0;

	typedef std::vector<SensorBase::Ptr> TListSensors;

	const TListSensors& getSensors() const { return m_sensors; }
	TListSensors& getSensors() { return m_sensors; }
	std::shared_ptr<CSVLogger> getLoggerPtr(std::string logger_name)
	{
		return m_loggers[logger_name];
	}

	/** User-supplied name of the vehicle (e.g. "r1", "veh1") */
	const std::string& getName() const { return m_name; }
	/** Get the 2D shape of the vehicle chassis, as set from the config file
	 * (only used for collision detection) */
	const mrpt::math::TPolygon2D& getChassisShape() const
	{
		return m_chassis_poly;
	}

	/** Set the vehicle index in the World */
	void setVehicleIndex(size_t idx) { m_vehicle_index = idx; }
	/** Get the vehicle index in the World */
	size_t getVehicleIndex() const { return m_vehicle_index; }
	void setRecording(bool record)
	{
		for (auto& logger : m_loggers) logger.second->setRecording(record);
	}
	void clearLogs()
	{
		for (auto& logger : m_loggers) logger.second->clear();
	}
	void newLogSession()
	{
		for (auto& logger : m_loggers) logger.second->newSession();
	}

	virtual ControllerBaseInterface* getControllerInterface() = 0;

   protected:
	std::map<std::string, std::shared_ptr<CSVLogger>> m_loggers;
	std::string m_log_path;

	virtual void initLoggers();
	virtual void writeLogStrings();
	virtual void internalGuiUpdate(mrpt::opengl::COpenGLScene& scene) override;

   protected:
	// Protected ctor for class factory
	VehicleBase(World* parent, size_t nWheels);

	/** Parse node <dynamics>: The derived-class part of load_params_from_xml(),
	 * also called in factory(). Includes parsing the <controller></controller>
	 * block. */
	virtual void dynamics_load_params_from_xml(
		const rapidxml::xml_node<char>* xml_node) = 0;

	virtual void invoke_motor_controllers(
		const TSimulContext& context,
		std::vector<double>& out_force_per_wheel) = 0;

	/** To be called at derived classes' internalGuiUpdate(), updates all stuff
	 * common to any vehicle type. Calls: internal_internalGuiUpdate_sensors(),
	 * internal_internalGuiUpdate_forces() \param[in] defaultVehicleBody If
	 * true, will draw default wheels & vehicle chassis.
	 */
	void internalGuiUpdate_common(
		mrpt::opengl::COpenGLScene& scene, bool defaultVehicleBody = true);

	std::string
		m_name;	 //!< User-supplied name of the vehicle (e.g. "r1", "veh1")
	size_t m_vehicle_index;	 //!< user-supplied index number: must be set/get'ed
							 //! with setVehicleIndex() getVehicleIndex()
							 //!(default=0)

	/** Derived classes must store here the body of the vehicle main body
	 * (chassis).
	 * This is used by \a simul_post_timestep() to extract the vehicle
	 * dynamical coords (q,\dot{q}) after each simulation step.
	 */
	b2Body* m_b2d_vehicle_body;

	FrictionBasePtr m_friction;	 //!< Instance of friction model for the
								 //! vehicle-to-ground interaction.

	TListSensors m_sensors;	 //!< Sensors aboard

	mrpt::math::TPose3D
		m_q;  //!< Last time-step pose (of the ref. point, in global coords)
	vec3 m_dq;	//!< Last time-step velocity (of the ref. point, in global
				//! coords)

	std::vector<double>
		m_torque_per_wheel;	 //!< Updated in simul_pre_timestep()

	// Chassis info:
	double m_chassis_mass;
	mrpt::math::TPolygon2D m_chassis_poly;
	double m_max_radius;  //!< Automatically computed from m_chassis_poly upon
						  //! each change via updateMaxRadiusFromPoly()
	double m_chassis_z_min, m_chassis_z_max;
	mrpt::img::TColor m_chassis_color;

	mrpt::math::TPoint2D m_chassis_com;	 //!< In local coordinates (this
										 //! excludes the mass of wheels)

	void updateMaxRadiusFromPoly();

	// Wheels info:
	std::vector<Wheel> m_wheels_info;  //!< The fixed size of this vector is set
									   //! upon construction. Derived classes
									   //! must define the order of the wheels,
									   //! e.g. [0]=rear left, etc.

	// Box2D elements:
	b2Fixture* m_fixture_chassis;  //!< Created at
	std::vector<b2Fixture*> m_fixture_wheels;  //!< [0]:rear-left, etc.
											   //!(depending on derived class).
											   //! Size set at constructor.

   private:
	void internal_internalGuiUpdate_sensors(
		mrpt::opengl::COpenGLScene&
			scene);	 //!< Called from internalGuiUpdate_common()
	void internal_internalGuiUpdate_forces(
		mrpt::opengl::COpenGLScene&
			scene);	 //!< Called from internalGuiUpdate_common()

	mrpt::opengl::CSetOfObjects::Ptr m_gl_chassis;
	std::vector<mrpt::opengl::CSetOfObjects::Ptr> m_gl_wheels;
	mrpt::opengl::CSetOfLines::Ptr m_gl_forces;
	std::mutex m_force_segments_for_rendering_cs;
	std::vector<mrpt::math::TSegment3D> m_force_segments_for_rendering;

   public:	// data logger header entries
	static constexpr char DL_TIMESTAMP[] = "timestamp";
	static constexpr char LOGGER_POSE[] = "logger_pose";
	static constexpr char LOGGER_WHEEL[] = "logger_wheel";

	static constexpr char PL_Q_X[] = "Qx";
	static constexpr char PL_Q_Y[] = "Qy";
	static constexpr char PL_Q_Z[] = "Qz";
	static constexpr char PL_Q_YAW[] = "Qyaw";
	static constexpr char PL_Q_PITCH[] = "Qpitch";
	static constexpr char PL_Q_ROLL[] = "Qroll";
	static constexpr char PL_DQ_X[] = "dQx";
	static constexpr char PL_DQ_Y[] = "dQy";
	static constexpr char PL_DQ_Z[] = "dQz";

	static constexpr char WL_TORQUE[] = "torque";
	static constexpr char WL_WEIGHT[] = "weight";
	static constexpr char WL_VEL_X[] = "velocity_x";
	static constexpr char WL_VEL_Y[] = "velocity_y";
	static constexpr char WL_FRIC_X[] = "friction_x";
	static constexpr char WL_FRIC_Y[] = "friction_y";
};	// end VehicleBase

// Class factory:
typedef ClassFactory<VehicleBase, World*> TClassFactory_vehicleDynamics;
extern TClassFactory_vehicleDynamics classFactory_vehicleDynamics;

#define DECLARES_REGISTER_VEHICLE_DYNAMICS(CLASS_NAME) \
	DECLARES_REGISTER_CLASS1(CLASS_NAME, VehicleBase, World*)

#define REGISTER_VEHICLE_DYNAMICS(TEXTUAL_NAME, CLASS_NAME)          \
	REGISTER_CLASS1(                                                 \
		TClassFactory_vehicleDynamics, classFactory_vehicleDynamics, \
		TEXTUAL_NAME, CLASS_NAME)
}  // namespace mvsim
