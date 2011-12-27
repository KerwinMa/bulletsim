#include "simplescene.h"
#include "thread_socket_interface.h"
#include "util.h"
#include "userconfig.h"

#include <iostream>
using namespace std;


Scene::Scene() {
    osg.reset(new OSGInstance());
    bullet.reset(new BulletInstance());
    bullet->setGravity(CFG.bullet.gravity);
    if (CFG.scene.enableRobot)
        rave.reset(new RaveInstance());

    env.reset(new Environment(bullet, osg));

    if (CFG.scene.enableHaptics)
        connectionInit(); // socket connection for haptics

    // populate the scene with some basic objects
    boost::shared_ptr<btDefaultMotionState> ms;
    ms.reset(new btDefaultMotionState(btTransform(btQuaternion(0, 0, 0, 1), btVector3(0, 0, 0))));
    ground.reset(new PlaneStaticObject(btVector3(0., 0., 1.), 0., ms));
    env->add(ground);

    if (CFG.scene.enableRobot) {
      btTransform trans(btQuaternion(0, 0, 0, 1), btVector3(0, 0, 0));
      pr2.reset(new RaveRobotKinematicObject(rave, "robots/pr2-beta-sim.robot.xml", trans, CFG.scene.scale));
      env->add(pr2);
      pr2->ignoreCollisionWith(ground->rigidBody.get()); // the robot's always touching the ground anyway
    }

    if (CFG.scene.enableIK && CFG.scene.enableRobot) {
        pr2Left = pr2->createManipulator("leftarm");
        pr2Right = pr2->createManipulator("rightarm");
        env->add(pr2Left->grabber);
        env->add(pr2Right->grabber);
    }
}

void Scene::startViewer() {
    drawingOn = syncTime = true;
    loopState.looping = loopState.paused = false;

    dbgDraw.reset(new osgbCollision::GLDebugDrawer());
    dbgDraw->setDebugMode(btIDebugDraw::DBG_MAX_DEBUG_DRAW_MODE /*btIDebugDraw::DBG_DrawWireframe*/);
    dbgDraw->setEnabled(false);
    bullet->dynamicsWorld->setDebugDrawer(dbgDraw.get());
    osg->root->addChild(dbgDraw->getSceneGraph());

    viewer.setUpViewInWindow(0, 0, CFG.viewer.windowWidth, CFG.viewer.windowHeight);
    manip = new EventHandler(this);
    manip->setHomePosition(util::toOSGVector(CFG.viewer.cameraHomePosition), osg::Vec3(), osg::Z_AXIS);
    viewer.setCameraManipulator(manip);
    viewer.setSceneData(osg->root.get());
    viewer.realize();
}

void Scene::processHaptics() {
    if (!CFG.scene.enableRobot)
        return;

    // read the haptic controllers
    btTransform trans0, trans1;
    bool buttons0[2], buttons1[2];
    static bool lastButton[2] = { false, false };
    if (!util::getHapticInput(trans0, buttons0, trans1, buttons1))
        return;

    pr2Left->moveByIK(trans0, CFG.scene.enableRobotCollision, true);
    if (buttons0[0] && !lastButton[0])
        pr2Left->grabber->grabNearestObjectAhead();
    else if (!buttons0[0] && lastButton[0])
        pr2Left->grabber->releaseConstraint();
    lastButton[0] = buttons0[0];

    pr2Right->moveByIK(trans0, CFG.scene.enableRobotCollision, true);
    if (buttons1[0] && !lastButton[1])
        pr2Right->grabber->grabNearestObjectAhead();
    else if (!buttons1[0] && lastButton[1])
        pr2Right->grabber->releaseConstraint();
    lastButton[1] = buttons1[0];
}

void Scene::step(float dt, int maxsteps, float internaldt) {
    static float startTime=viewer.getFrameStamp()->getSimulationTime(), endTime;

    if (syncTime && drawingOn)
        endTime = viewer.getFrameStamp()->getSimulationTime();

    if (CFG.scene.enableHaptics)
        processHaptics();
    env->step(dt, maxsteps, internaldt);
    draw();

    if (syncTime && drawingOn) {
        float timeLeft = dt - (endTime - startTime);
        idleFor(timeLeft);
        startTime = endTime + timeLeft;
    }
}

void Scene::step(float dt) {
    step(dt, CFG.bullet.maxSubSteps, CFG.bullet.internalTimeStep);
}

// Steps for a time interval
void Scene::stepFor(float dt, float time) {
    while (time > 0) {
        step(dt);
        time -= dt;
    }
}

// Idles for a time interval. Physics will not run.
void Scene::idleFor(float time) {
    if (!drawingOn || !syncTime || time <= 0.f)
        return;
    float endTime = time + viewer.getFrameStamp()->getSimulationTime();
    while (viewer.getFrameStamp()->getSimulationTime() < endTime && !viewer.done())
        draw();
}

void Scene::draw() {
    if (!drawingOn)
        return;
    if (manip->state.debugDraw) {
        dbgDraw->BeginDraw();
        bullet->dynamicsWorld->debugDrawWorld();
        dbgDraw->EndDraw();
    }
    viewer.frame();
}

void Scene::startLoop() {
    bool oldSyncTime = syncTime;
    syncTime = false;
    loopState.looping = true;
    loopState.prevTime = loopState.currTime =
        viewer.getFrameStamp()->getSimulationTime();
    while (loopState.looping && drawingOn && !viewer.done()) {
        loopState.currTime = viewer.getFrameStamp()->getSimulationTime();
        step(loopState.currTime - loopState.prevTime);
        loopState.prevTime = loopState.currTime;
    }
    syncTime = oldSyncTime;
}

void Scene::startFixedTimestepLoop(float dt) {
    loopState.looping = true;
    while (loopState.looping && drawingOn && !viewer.done())
        step(dt);
}

void Scene::stopLoop() {
    loopState.looping = false;
}

void Scene::idle(bool b) {
    loopState.paused = b;
    while (loopState.paused && drawingOn && !viewer.done())
        draw();
    loopState.prevTime = loopState.currTime = viewer.getFrameStamp()->getSimulationTime();
}

void Scene::toggleIdle() {
    idle(!loopState.paused);
}

void Scene::runAction(Action &a, float dt) {
    while (!a.done()) {
        a.step(dt);
        step(dt);
    }
}

void EventHandler::getTransformation( osg::Vec3d& eye, osg::Vec3d& center, osg::Vec3d& up ) const
  {
    center = _center;
    eye = _center + _rotation * osg::Vec3d( 0., 0., _distance );
    up = _rotation * osg::Vec3d( 0., 1., 0. );
  }

bool EventHandler::handle(const osgGA::GUIEventAdapter &ea, osgGA::GUIActionAdapter &aa) {
    switch (ea.getEventType()) {
    case osgGA::GUIEventAdapter::KEYDOWN:
        switch (ea.getKey()) {
        case 'd':
            state.debugDraw = !state.debugDraw;
            scene->dbgDraw->setEnabled(state.debugDraw);
            break;
        case 'p':
            scene->toggleIdle();
            break;
        case '1':
            state.moveGrabber0 = true; break;
        case '2':
            state.moveGrabber1 = true; break;
        case 'q':
            state.rotateGrabber0 = true; break;
        case 'w':
            state.rotateGrabber1 = true; break;
      }
      break;

    case osgGA::GUIEventAdapter::KEYUP:
        switch (ea.getKey()) {
        case '1':
            state.moveGrabber0 = false; break;
        case '2':
            state.moveGrabber1 = false; break;
        case 'q':
            state.rotateGrabber0 = false; break;
        case 'w':
            state.rotateGrabber1 = false; break;
        }
        break;


    case osgGA::GUIEventAdapter::PUSH:
        state.startDragging = true;
        return osgGA::TrackballManipulator::handle(ea, aa);

    case osgGA::GUIEventAdapter::DRAG:
        // drag the active grabber in the plane of view
        if (CFG.scene.enableRobot && CFG.scene.enableIK &&
              (ea.getButtonMask() & ea.LEFT_MOUSE_BUTTON) &&
              (state.moveGrabber0 || state.moveGrabber1 ||
               state.rotateGrabber0 || state.rotateGrabber1)) {
            if (state.startDragging) {
                dx = dy = 0;
            } else {
                dx = lastX - ea.getXnormalized();
                dy = ea.getYnormalized() - lastY;
            }
            lastX = ea.getXnormalized(); lastY = ea.getYnormalized();
            state.startDragging = false;
  
            // get our current view
            osg::Vec3d osgCenter, osgEye, osgUp;
            getTransformation(osgCenter, osgEye, osgUp);
            btVector3 from(util::toBtVector(osgEye));
            btVector3 to(util::toBtVector(osgCenter));
            btVector3 up(util::toBtVector(osgUp)); up.normalize();
  
            // compute basis vectors for the plane of view
            // (the plane normal to the ray from the camera to the center of the scene)
            btVector3 normal = (to - from).normalized();
            btVector3 yVec = (up - (up.dot(normal))*normal).normalized(); //FIXME: is this necessary with osg?
            btVector3 xVec = normal.cross(yVec);
            btVector3 dragVec = CFG.scene.mouseDragScale * (dx*xVec + dy*yVec);

            // now set the position of the grabber
            RaveRobotKinematicObject::Manipulator::Ptr manip;
            if (state.moveGrabber0 || state.rotateGrabber0)
                manip = scene->pr2Left;
            else
                manip = scene->pr2Right;

            btTransform origTrans = manip->getTransform();
            btTransform newTrans(origTrans);

            if (state.moveGrabber0 || state.moveGrabber1)
                // if moving the grabber, just set the origin appropriately
                newTrans.setOrigin(dragVec + origTrans.getOrigin());
            else if (state.rotateGrabber0 || state.rotateGrabber1) {
                // if we're rotating, the axis is perpendicular to the
                // direction the mouse is dragging
                btVector3 axis = normal.cross(dragVec);
                btScalar angle = dragVec.length();
                btQuaternion rot(axis, angle);
                // we must ensure that we never get a bad rotation quaternion
                // due to really small (effectively zero) mouse movements
                // this is the easiest way to do this:
                if (rot.length() > 0.99f && rot.length() < 1.01f)
                    newTrans.setRotation(rot * origTrans.getRotation());
            }
            manip->moveByIK(newTrans, CFG.scene.enableRobotCollision, true);
        } else {
            // if not dragging, we want the camera to move
            return osgGA::TrackballManipulator::handle(ea, aa);
        }
        break;

    default:
        return osgGA::TrackballManipulator::handle(ea, aa);
    }
    // this event handler doesn't actually change the camera, so return false
    // to let other handlers deal with this event too
    return false;
}
