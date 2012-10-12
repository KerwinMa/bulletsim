#pragma once
#include "utils_sqp.h"
#include "traj_costs.h"
#include "simulation/openravesupport.h"
#include "simulation/fake_gripper.h"
#include "simulation/plotting.h"
#include "gurobi_c++.h"
#include "sqp_fwd.h"


inline std::string base_filename(char* in) {
  std::string s(in);
  size_t ind = s.rfind('/');
  if (ind == std::string::npos ) return s;
  else return s.substr(ind+1, std::string::npos);
}

#define ASSERT_ALMOST_EQUAL(a,b,tol)\
{\
  if ( !( 2 * fabs(a-b) / (fabs(a)+fabs(b)) < tol) )  {\
    printf("%s != %s (%.4e != %.4e) in file %s at line %i\n", #a, #b, a, b, base_filename(__FILE__).c_str(), __LINE__ );\
    exit(1);\
  }\
}

#define ASSERT_ALMOST_EQUAL2(a,b,reltol, abstol)\
	{\
	  if ( !( 2 * fabs(a-b) / (fabs(a)+fabs(b)) < reltol || fabs(a-b) < abstol) )  {\
	    printf("%s != %s (%.4e != %.4e) in file %s at line %i\n", #a, #b, a, b, base_filename(__FILE__).c_str(), __LINE__ );\
	    exit(1);\
				}\
	}



GRBEnv* getGRBEnv();

class Scene;

typedef BasicArray<GRBVar> VarArray;
typedef std::vector<GRBVar> VarVector;
typedef Eigen::Matrix<bool,Eigen::Dynamic,1> VectorXb;

ArmCCEPtr makeArmCCE(RaveRobotObject::Manipulator::Ptr arm, RaveRobotObject::Ptr robot, btDynamicsWorld*);
BulletRaveSyncherPtr syncherFromArm(RaveRobotObject::Manipulator::Ptr rrom);


class PlanningProblem { // fixed size optimization problem
public:
  typedef boost::function<void(PlanningProblem*)> Callback;
  VarArray m_trajVars; // trajectory variables in optimization. sometimes the endpoints are not actually added to Gurobi model
  Eigen::MatrixXd m_currentTraj;
  VectorXb m_optMask; // mask that indicates which timesteps are in the optimization
  boost::shared_ptr<GRBModel> m_model;
  std::vector<TrajPlotterPtr> m_plotters;
  std::vector<ProblemComponentPtr> m_comps;
  bool m_initialized; 
  std::vector<Callback> m_callbacks;
  Eigen::VectorXd m_times; // note that these aren't times in meaningful. they're just the integer indices
  TrustRegionAdjusterPtr m_tra; // this is the problem component that determines how much you're allowed to chnage at each iteration
	bool m_exactObjectiveReady, m_approxObjectiveReady;

  PlanningProblem();
  void addComponent(ProblemComponentPtr comp);
  void removeComponent(ProblemComponentPtr comp);
  std::vector<ProblemComponentPtr> getCostComponents();
  std::vector<ProblemComponentPtr> getConstraintComponents();
  void initialize(const Eigen::MatrixXd& initTraj, bool endFixed);
  void initialize(const Eigen::MatrixXd& initTraj, bool endFixed, const Eigen::VectorXd& times); 
  void optimize(int maxIter);
  void addPlotter(TrajPlotterPtr plotter) {m_plotters.push_back(plotter);}
  double calcApproxObjective(); 
  double calcExactObjective();
  void updateModel();
  void optimizeModel();
  void forceOptimizeHere();
  void subdivide(const std::vector<double>& insertTimes); // resample in time (resamples all subproblems)
  void testObjectives(); // checks that the linearizations computed by all of the objectives is correct by comparing calcApproxObjective and calcExactObjective
  void addTrustRegionAdjuster(TrustRegionAdjusterPtr tra); // actually there's only one right now so "add" is wrong
};

class ProblemComponent {
public:
  enum CompAttrs {
    RELAXABLE=1,
    HAS_CONSTRAINT=2,
    HAS_COST=4
  };
  unsigned int m_attrs;
  typedef boost::shared_ptr<ProblemComponent> Ptr;

  PlanningProblem* m_problem;

  ProblemComponent() {
    m_attrs = 0;
  }
  virtual ~ProblemComponent() {}
  virtual void onAdd() {}
  virtual void updateModel(const Eigen::MatrixXd& traj, GRBQuadExpr& objective) = 0;
  virtual double calcApproxObjective() {return 0;}
  virtual double calcExactObjective() {return 0;}
  virtual void relax() {}
  virtual void onRemove() {};
  virtual void reset() {
    onRemove();
    onAdd();
  }
  virtual void subdivide(const std::vector<double>& insertTimes, const Eigen::VectorXd& oldTimes, const Eigen::VectorXd& newTimes) {}
};

class CollisionCost: public ProblemComponent {
public:
  std::vector<GRBVar> m_vars;
  std::vector<GRBConstr> m_cnts;
  GRBLinExpr m_obj;

  OpenRAVE::RobotBasePtr m_robot;
  btDynamicsWorld* m_world;
  BulletRaveSyncherPtr m_brs;
  vector<int> m_dofInds;

  int m_start, m_end;
  double m_distPen;
  double m_coeff;
  double m_exactObjective;
  TrajCartCollInfo m_cartCollInfo;
  Eigen::VectorXd m_coeffVec;
  void removeVariablesAndConstraints();
  CollisionCost(OpenRAVE::RobotBasePtr robot, btDynamicsWorld* world, BulletRaveSyncherPtr brs, const vector<int>& dofInds, double distPen, double coeff) :
    m_robot(robot), m_world(world), m_brs(brs), m_dofInds(dofInds), m_distPen(distPen), m_coeff(coeff) {
      m_attrs = HAS_COST + HAS_CONSTRAINT;
    }
  void updateModel(const Eigen::MatrixXd& traj, GRBQuadExpr& objective);
  double calcApproxObjective() {return m_obj.getValue();}
  double calcExactObjective() {return m_exactObjective;}
  void onRemove();
  void setCoeff(double coeff) {m_coeff = coeff;}
  void setCoeffVec(const Eigen::VectorXd& coeffVec) {m_coeffVec = coeffVec;}
  void subdivide(const std::vector<double>& insertTimes, const Eigen::VectorXd& oldTimes, const Eigen::VectorXd& newTimes);
};



#if 0
class VelScaledCollisionCost : public CollisionCost {
public:
  void updateModel(const Eigen::MatrixXd& traj, GRBQuadExpr& objective);
};
#endif

#if 0
class CollisionConstraint: public ProblemComponent {
protected:
  std::vector<GRBConstr> m_cnts;
  CollisionCostEvaluatorPtr m_cce;
  double m_distPen;
  double m_coeff;
  bool m_startFixed, m_endFixed;
public:
  CollisionConstraint(bool startFixed, bool endFixed, CollisionCostEvaluatorPtr cce, double safeDist) :
    m_startFixed(startFixed), m_endFixed(endFixed), m_cce(cce), m_distPen(safeDist) {
    m_attrs = RELAXABLE + HAS_CONSTRAINT;
  }
  void onAdd();
  void updateModel(const Eigen::MatrixXd& traj, GRBQuadExpr& objective);
  void onRemove();
  void relax();
};
#endif

class TrustRegionAdjuster : public ProblemComponent {
public:
  virtual void adjustTrustRegion(double ratio)=0;
};

class LengthConstraintAndCost : public ProblemComponent {
  // todo: set this
  GRBQuadExpr m_obj;//UNSCALED OBJECTIVE
  std::vector<GRBConstr> m_cnts;
  bool m_startFixed, m_endFixed;
  Eigen::VectorXd m_maxStepMvmt;
  double m_coeff;
public:
  LengthConstraintAndCost(bool startFixed, bool endFixed, const Eigen::VectorXd& maxStepMvmt, double coeff) :
    m_startFixed(startFixed), m_endFixed(endFixed), m_maxStepMvmt(maxStepMvmt), m_coeff(coeff) {
      m_attrs = HAS_CONSTRAINT + HAS_COST;
    }

  void onAdd();
  void updateModel(const Eigen::MatrixXd& traj, GRBQuadExpr& objective) { objective += m_coeff*m_obj;}
  double calcApproxObjective() { return m_coeff*m_obj.getValue(); }
  double calcExactObjective();
  void onRemove();
  void setCoeff(double coeff) {m_coeff = coeff;}
  void subdivide(const std::vector<double>& insertTimes, const Eigen::VectorXd& oldTimes, const Eigen::VectorXd& newTimes);
};

class JointBounds: public TrustRegionAdjuster {
  // todo: set these in constructor
  Eigen::VectorXd m_jointLowerLimit;
  Eigen::VectorXd m_jointUpperLimit;
  Eigen::VectorXd m_maxDiffPerIter;
  bool m_startFixed, m_endFixed;
public:
  JointBounds(bool startFixed, bool endFixed, const Eigen::VectorXd& maxDiffPerIter, OpenRAVE::RobotBase::ManipulatorPtr manip) :
    m_startFixed(startFixed), m_endFixed(endFixed), m_maxDiffPerIter(maxDiffPerIter) {
      m_attrs = HAS_CONSTRAINT;
    vector<int> armInds = manip->GetArmIndices();
    m_jointLowerLimit.resize(armInds.size());
    m_jointUpperLimit.resize(armInds.size());
    vector<double> ul, ll;
    for (int i=0; i < armInds.size(); ++i) {
      manip->GetRobot()->GetJointFromDOFIndex(armInds[i])->GetLimits(ll, ul);
      m_jointLowerLimit(i) = ll[0];
      m_jointUpperLimit(i) = ul[0];
    }
  }

  void onAdd();
  void onRemove() {}
  void updateModel(const Eigen::MatrixXd& traj, GRBQuadExpr& objective);
  void adjustTrustRegion(double ratio);
};


class CartesianPoseCost: public ProblemComponent {
  GRBQuadExpr m_obj;
  RaveRobotObject::Manipulator::Ptr m_manip;
  Eigen::Vector3d m_posTarg;
  Eigen::Vector4d m_rotTarg;
  double m_posCoeff, m_rotCoeff;
  int m_timestep;
public:
  CartesianPoseCost(RaveRobotObject::Manipulator::Ptr manip,
      const btTransform& target, int timestep, double posCoeff, double rotCoeff) :
        m_manip(manip),
        m_posTarg(toVector3d(target.getOrigin())),
        m_rotTarg(toVector4d(target.getRotation())),
        m_posCoeff(posCoeff),
        m_rotCoeff(rotCoeff),
        m_timestep(timestep) {
    m_attrs = HAS_COST;
  }

  void updateModel(const Eigen::MatrixXd& traj, GRBQuadExpr& objective);
  double calcApproxObjective() {return m_obj.getValue();}
  double calcExactObjective();
  void subdivide(const std::vector<double>& insertTimes, const Eigen::VectorXd& oldTimes, const Eigen::VectorXd& newTimes);
};

class CartesianPoseConstraint: public ProblemComponent {
  std::vector<GRBConstr> m_cnts;
  RaveRobotObject::Manipulator::Ptr m_manip;
  Eigen::Vector3d m_posTarg;
  Eigen::Vector4d m_rotTarg;
  double m_posTol, m_rotTol;
  int m_timestep;
  void removeVariablesAndConstraints();
public:
  CartesianPoseConstraint(RaveRobotObject::Manipulator::Ptr manip,
      const btTransform& target, int timestep, double posTol, double rotTol) :
        m_manip(manip),
        m_posTarg(toVector3d(target.getOrigin())),
        m_rotTarg(toVector4d(target.getRotation())),
        m_posTol(posTol),
        m_rotTol(rotTol),
        m_timestep(timestep) {
    m_attrs = HAS_CONSTRAINT;
  }

  void updateModel(const Eigen::MatrixXd& traj, GRBQuadExpr& objective);
  void onRemove();
  void subdivide(const std::vector<double>& insertTimes, const Eigen::VectorXd& oldTimes, const Eigen::VectorXd& newTimes);
};


class CartesianVelConstraint : public ProblemComponent {
  GRBQuadExpr m_obj;
  std::vector<GRBConstr> m_cnts;
  RaveRobotObject::Manipulator::Ptr m_manip;
  Eigen::Vector3d m_posTarg;
  Eigen::Vector4d m_rotTarg;
  int m_start, m_stop;
  double m_maxDist;
  void removeVariablesAndConstraints();
public:
  CartesianVelConstraint(RaveRobotObject::Manipulator::Ptr manip, int start, int stop, double maxDist) :
        m_manip(manip),
        m_start(start),
        m_stop(stop),
        m_maxDist(maxDist) {
    m_attrs = HAS_CONSTRAINT;
  }

  void updateModel(const Eigen::MatrixXd& traj, GRBQuadExpr& objective);
  double calcApproxObjective() {return m_obj.getValue();}
  void onRemove();
  void subdivide(const std::vector<double>& insertTimes, const Eigen::VectorXd& oldTimes, const Eigen::VectorXd& newTimes);
};



Eigen::VectorXd defaultMaxStepMvmt(const Eigen::MatrixXd& traj);
Eigen::MatrixXd makeTraj(const Eigen::VectorXd& startJoints, const Eigen::VectorXd& endJoints, int nSteps);
Eigen::MatrixXd makeTraj(RaveRobotObject::Manipulator::Ptr manip, const Eigen::VectorXd& startJoints, const btTransform endTransform, int nSteps);
Eigen::MatrixXd makeTraj(RaveRobotObject::Manipulator::Ptr manip, const std::vector<btTransform>& transforms);
void updateTraj(const VarArray& trajVars, const VectorXb& optmask, Eigen::MatrixXd& traj);


class TrajPlotter {
public:
  typedef boost::shared_ptr<TrajPlotter> Ptr;
  virtual void plotTraj(const Eigen::MatrixXd& traj) = 0;
  virtual void clear() {}
  virtual ~TrajPlotter() {}
};

class GripperPlotter : public TrajPlotter {
public:
  std::vector<FakeGripper::Ptr> m_grippers;
  PlotCurve::Ptr m_curve;
  RaveRobotObject::Manipulator::Ptr m_rrom;
  Scene* m_scene;
  osg::Group* m_osgRoot;
  int m_decimation;
  void plotTraj(const Eigen::MatrixXd& traj);
  void clear();
  GripperPlotter(RaveRobotObject::Manipulator::Ptr, Scene*, int decimation=1);
  ~GripperPlotter();
  void setNumGrippers(int n);
};

class ArmPlotter : public TrajPlotter {
public:
  typedef boost::shared_ptr<ArmPlotter> Ptr;
  RaveRobotObject::Manipulator::Ptr m_rrom;
  std::vector<BulletObject::Ptr> m_origs;
  BasicArray<FakeObjectCopy::Ptr> m_fakes;
  PlotCurve::Ptr m_curve;
  PlotAxes::Ptr m_axes;
  Scene* m_scene;
  osg::Group* m_osgRoot;
  int m_decimation;
  BulletRaveSyncherPtr m_syncher;
  void plotTraj(const Eigen::MatrixXd& traj);
  ArmPlotter(RaveRobotObject::Manipulator::Ptr rrom, Scene* scene, int decimation=1);
  void init(RaveRobotObject::Manipulator::Ptr, const std::vector<BulletObject::Ptr>&, Scene*, int decimation);
  void setLength(int n);
  void clear() {setLength(0);}
  ~ArmPlotter();
};

void interactiveTrajPlot(const Eigen::MatrixXd& traj, RaveRobotObject::Manipulator::Ptr arm, Scene* scene);
void plotCollisions(const TrajCartCollInfo& trajCartInfo, double safeDist);



