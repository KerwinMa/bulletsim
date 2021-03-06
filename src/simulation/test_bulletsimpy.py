import openravepy as rave
import bulletsimpy

env = rave.Environment()
env.SetViewer('qtcoin')

env.Load('data/lab1.env.xml')

print 'number of objects in environment:', len(env.GetBodies())
print env.GetBodies()

dyn_obj_names = ['mug1', 'mug2', 'mug3', 'mug4', 'mug5']


import time

t0 = time.time()
bullet_env = bulletsimpy.BulletEnvironment(env, dyn_obj_names)
bullet_env.SetGravity([0, 0, -9.8])
dyn_objs = [bullet_env.GetObjectByName(b.GetName()) for b in env.GetBodies() if b.GetName() in dyn_obj_names]
for t in range(20):
  bullet_env.Step(0.01, 100, 0.01)
for o in dyn_objs:
  o.UpdateRave()
env.UpdatePublishedBodies()
print time.time() - t0

bullet_env = bulletsimpy.BulletEnvironment(env, dyn_obj_names)
bullet_env.SetGravity([0, 0, -9.8])
print 'gravity set to:', bullet_env.GetGravity()
bullet_objs = [bullet_env.GetObjectByName(b.GetName()) for b in env.GetBodies()]
print [(o.GetName(), o.IsKinematic()) for o in bullet_objs]
dyn_objs = [bullet_env.GetObjectByName(b.GetName()) for b in env.GetBodies() if b.GetName() in dyn_obj_names]
print 'dyn objs', [o.GetName() for o in dyn_objs]
robot_obj = bullet_env.GetObjectByName(env.GetRobots()[0].GetName())
TIMESTEPS = 100

mug1 = bullet_env.GetObjectByName('mug1')
T = mug1.GetTransform()
T[:3,3] += [0, 0, 1]
mug1.SetTransform(T)

for t in range(TIMESTEPS):
  print t

  for o in dyn_objs:
    print o.GetName()
    print o.GetTransform()
    o.UpdateRave()
  env.UpdatePublishedBodies()

  robot_obj.UpdateBullet()
  bullet_env.Step(0.01, 100, 0.01)

  print "Collisions:"
  collisions = bullet_env.DetectAllCollisions()
  for c in collisions:
    print 'linkA:', c.linkA.GetParent().GetName(), c.linkA.GetName()
    print 'linkB:', c.linkB.GetParent().GetName(), c.linkB.GetName()
    print 'ptA:', c.ptA
    print 'ptB:', c.ptB
    print 'normalB2A:', c.normalB2A
    print 'distance:', c.distance
    print 'weight:', c.weight

  raw_input('enter to step (next step: %d)' % (t+1))
