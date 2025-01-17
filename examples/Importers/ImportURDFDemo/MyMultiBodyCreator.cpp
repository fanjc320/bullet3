#include "MyMultiBodyCreator.h"

#include "../../CommonInterfaces/CommonGUIHelperInterface.h"

#include "BulletDynamics/Featherstone/btMultiBodyLinkCollider.h"
#include "BulletDynamics/Featherstone/btMultiBodyDynamicsWorld.h"

#include "BulletCollision/CollisionShapes/btCompoundShape.h"

#include "btBulletDynamicsCommon.h"
#include "BulletDynamics/ConstraintSolver/btGeneric6DofSpring2Constraint.h"
#include "URDFJointTypes.h"

MyMultiBodyCreator::MyMultiBodyCreator(GUIHelperInterface* guiHelper)
	: m_bulletMultiBody(0),
	  m_rigidBody(0),
	  m_guiHelper(guiHelper)
{
}

class btMultiBody* MyMultiBodyCreator::allocateMultiBody(int /* urdfLinkIndex */, int totalNumJoints, btScalar mass, const btVector3& localInertiaDiagonal, bool isFixedBase, bool canSleep)
{
	//	m_urdf2mbLink.resize(totalNumJoints+1,-2);
	m_mb2urdfLink.resize(totalNumJoints + 1, -2);

	m_bulletMultiBody = new btMultiBody(totalNumJoints, mass, localInertiaDiagonal, isFixedBase, canSleep);
	//if (canSleep)
	//	m_bulletMultiBody->goToSleep();
	return m_bulletMultiBody;
}

class btRigidBody* MyMultiBodyCreator::allocateRigidBody(int urdfLinkIndex, btScalar mass, const btVector3& localInertiaDiagonal, const btTransform& initialWorldTrans, class btCollisionShape* colShape)
{
	btRigidBody::btRigidBodyConstructionInfo rbci(mass, 0, colShape, localInertiaDiagonal);
	rbci.m_startWorldTransform = initialWorldTrans;
	btScalar sleep_threshold = btScalar(0.22360679775);//sqrt(0.05) to be similar to btMultiBody (SLEEP_THRESHOLD)
	rbci.m_angularSleepingThreshold = sleep_threshold;
	rbci.m_linearSleepingThreshold = sleep_threshold;
	
	btRigidBody* body = new btRigidBody(rbci);
	if (m_rigidBody == 0)
	{
		//only store the root of the multi body
		m_rigidBody = body;
	}
	return body;
}

class btMultiBodyLinkCollider* MyMultiBodyCreator::allocateMultiBodyLinkCollider(int /*urdfLinkIndex*/, int mbLinkIndex, btMultiBody* multiBody)
{
	btMultiBodyLinkCollider* mbCol = new btMultiBodyLinkCollider(multiBody, mbLinkIndex);
	return mbCol;
}

class btGeneric6DofSpring2Constraint* MyMultiBodyCreator::allocateGeneric6DofSpring2Constraint(int urdfLinkIndex, btRigidBody& rbA /*parent*/, btRigidBody& rbB, const btTransform& offsetInA, const btTransform& offsetInB, int rotateOrder)
{
	btGeneric6DofSpring2Constraint* c = new btGeneric6DofSpring2Constraint(rbA, rbB, offsetInA, offsetInB, (RotateOrder)rotateOrder);

	return c;
}

//Prismatic Joint的作用是在一个方向上移动某一个刚体，移动的最大距离和最小距离可以定义，移动过程中刚体不能转动。一般可以用着活塞运动。
class btGeneric6DofSpring2Constraint* MyMultiBodyCreator::createPrismaticJoint(int urdfLinkIndex, btRigidBody& rbA /*parent*/, btRigidBody& rbB, const btTransform& offsetInA, const btTransform& offsetInB,
																			   const btVector3& jointAxisInJointSpace, btScalar jointLowerLimit, btScalar jointUpperLimit)
{
	int rotateOrder = 0;
	btGeneric6DofSpring2Constraint* dof6 = allocateGeneric6DofSpring2Constraint(urdfLinkIndex, rbA, rbB, offsetInA, offsetInB, rotateOrder);
	//todo(erwincoumans) for now, we only support principle axis along X, Y or Z
	int principleAxis = jointAxisInJointSpace.closestAxis();

	GenericConstraintUserInfo* userInfo = new GenericConstraintUserInfo;
	userInfo->m_jointAxisInJointSpace = jointAxisInJointSpace;
	userInfo->m_jointAxisIndex = principleAxis;

	userInfo->m_urdfJointType = URDFPrismaticJoint;
	userInfo->m_lowerJointLimit = jointLowerLimit;
	userInfo->m_upperJointLimit = jointUpperLimit;
	userInfo->m_urdfIndex = urdfLinkIndex;
	dof6->setUserConstraintPtr(userInfo);

	switch (principleAxis)
	{
		case 0:
		{
			dof6->setLinearLowerLimit(btVector3(jointLowerLimit, 0, 0));
			dof6->setLinearUpperLimit(btVector3(jointUpperLimit, 0, 0));
			break;
		}
		case 1:
		{
			dof6->setLinearLowerLimit(btVector3(0, jointLowerLimit, 0));
			dof6->setLinearUpperLimit(btVector3(0, jointUpperLimit, 0));
			break;
		}
		case 2:
		default:
		{
			dof6->setLinearLowerLimit(btVector3(0, 0, jointLowerLimit));
			dof6->setLinearUpperLimit(btVector3(0, 0, jointUpperLimit));
		}
	};

	dof6->setAngularLowerLimit(btVector3(0, 0, 0));
	dof6->setAngularUpperLimit(btVector3(0, 0, 0));
	m_6DofConstraints.push_back(dof6);
	return dof6;
}

//Revolute 在这里是旋转的意思，Revolute Joint便是旋转关节的意思。 可以想象成我们日常生活中的门，拉一下，门就会围绕着墙边旋转。 和distance Joint一样，Revolute Joint也有两个锚点。锚点的位置是和对应的rigidbody绑定的。 在场景simulate之后，锚点会控制rigidbody移动和旋转。 Unity 3D中HingeJoint就是用Revolute Joint来实现的。 在Unity 3D中HingeJoint有三种功能：
//———————————————— 版权声明：本文为CSDN博主「听不懂道理」的原创文章，遵循CC 4.0 BY - SA版权协议，转载请附上原文出处链接及本声明。 原文链接：https :  //blog.csdn.net/xiazeye/article/details/78453787
//1.Spring 这个功能的作用就是 产生一个旋转的弹力。就像我们平时开了门，门的弹簧会自动把门扣上。 
//2.Limit 这个功能的作用就是设置Joint的极限。就像我们平时用力把门关上，门的内侧撞到墙会把门反弹回来 
//3.Motor 这个功能的作用就是给Joint设置一个旋转速度，像发动机一样

class btGeneric6DofSpring2Constraint* MyMultiBodyCreator::createRevoluteJoint(int urdfLinkIndex, btRigidBody& rbA /*parent*/, btRigidBody& rbB, const btTransform& offsetInA, const btTransform& offsetInB,
																			  const btVector3& jointAxisInJointSpace, btScalar jointLowerLimit, btScalar jointUpperLimit)
{
	btGeneric6DofSpring2Constraint* dof6 = 0;

	//only handle principle axis at the moment,
	//@todo(erwincoumans) orient the constraint for non-principal axis
	int principleAxis = jointAxisInJointSpace.closestAxis();
	switch (principleAxis)
	{
		case 0:
		{
			dof6 = allocateGeneric6DofSpring2Constraint(urdfLinkIndex, rbA, rbB, offsetInA, offsetInB, RO_ZYX);
			dof6->setLinearLowerLimit(btVector3(0, 0, 0));
			dof6->setLinearUpperLimit(btVector3(0, 0, 0));

			dof6->setAngularLowerLimit(btVector3(jointLowerLimit, 0, 0));
			dof6->setAngularUpperLimit(btVector3(jointUpperLimit, 0, 0));

			break;
		}
		case 1:
		{
			dof6 = allocateGeneric6DofSpring2Constraint(urdfLinkIndex, rbA, rbB, offsetInA, offsetInB, RO_XZY);
			dof6->setLinearLowerLimit(btVector3(0, 0, 0));
			dof6->setLinearUpperLimit(btVector3(0, 0, 0));

			dof6->setAngularLowerLimit(btVector3(0, jointLowerLimit, 0));
			dof6->setAngularUpperLimit(btVector3(0, jointUpperLimit, 0));
			break;
		}
		case 2:
		default:
		{
			dof6 = allocateGeneric6DofSpring2Constraint(urdfLinkIndex, rbA, rbB, offsetInA, offsetInB, RO_XYZ);
			dof6->setLinearLowerLimit(btVector3(0, 0, 0));
			dof6->setLinearUpperLimit(btVector3(0, 0, 0));

			dof6->setAngularLowerLimit(btVector3(0, 0, jointLowerLimit));
			dof6->setAngularUpperLimit(btVector3(0, 0, jointUpperLimit));
		}
	};

	GenericConstraintUserInfo* userInfo = new GenericConstraintUserInfo;
	userInfo->m_jointAxisInJointSpace = jointAxisInJointSpace;
	userInfo->m_jointAxisIndex = 3 + principleAxis;

	if (jointLowerLimit > jointUpperLimit)
	{
		userInfo->m_urdfJointType = URDFContinuousJoint;
	}
	else
	{
		userInfo->m_urdfJointType = URDFRevoluteJoint;
		userInfo->m_lowerJointLimit = jointLowerLimit;
		userInfo->m_upperJointLimit = jointUpperLimit;
	}
	userInfo->m_urdfIndex = urdfLinkIndex;
	dof6->setUserConstraintPtr(userInfo);
	m_6DofConstraints.push_back(dof6);
	return dof6;
}

class btGeneric6DofSpring2Constraint* MyMultiBodyCreator::createFixedJoint(int urdfLinkIndex, btRigidBody& rbA /*parent*/, btRigidBody& rbB, const btTransform& offsetInA, const btTransform& offsetInB)
{
	btGeneric6DofSpring2Constraint* dof6 = allocateGeneric6DofSpring2Constraint(urdfLinkIndex, rbA, rbB, offsetInA, offsetInB);

	GenericConstraintUserInfo* userInfo = new GenericConstraintUserInfo;
	userInfo->m_urdfIndex = urdfLinkIndex;
	userInfo->m_urdfJointType = URDFFixedJoint;

	dof6->setUserConstraintPtr(userInfo);

	dof6->setLinearLowerLimit(btVector3(0, 0, 0));
	dof6->setLinearUpperLimit(btVector3(0, 0, 0));

	dof6->setAngularLowerLimit(btVector3(0, 0, 0));
	dof6->setAngularUpperLimit(btVector3(0, 0, 0));
	m_6DofConstraints.push_back(dof6);
	return dof6;
}

void MyMultiBodyCreator::addLinkMapping(int urdfLinkIndex, int mbLinkIndex)
{
	if (m_mb2urdfLink.size() < (mbLinkIndex + 1))
	{
		m_mb2urdfLink.resize((mbLinkIndex + 1), -2);
	}
	//    m_urdf2mbLink[urdfLinkIndex] = mbLinkIndex;
	m_mb2urdfLink[mbLinkIndex] = urdfLinkIndex;
}

void MyMultiBodyCreator::createRigidBodyGraphicsInstance(int linkIndex, btRigidBody* body, const btVector3& colorRgba, int graphicsIndex)
{
	m_guiHelper->createRigidBodyGraphicsObject(body, colorRgba);
}

void MyMultiBodyCreator::createRigidBodyGraphicsInstance2(int linkIndex, class btRigidBody* body, const btVector3& colorRgba, const btVector3& specularColor, int graphicsIndex)
{
	m_guiHelper->createRigidBodyGraphicsObject(body, colorRgba);
	int graphicsInstanceId = body->getUserIndex();
	btVector3DoubleData speculard;
	specularColor.serializeDouble(speculard);
	m_guiHelper->changeSpecularColor(graphicsInstanceId, speculard.m_floats);
}

void MyMultiBodyCreator::createCollisionObjectGraphicsInstance(int linkIndex, class btCollisionObject* colObj, const btVector3& colorRgba)
{
	m_guiHelper->createCollisionObjectGraphicsObject(colObj, colorRgba);
}

void MyMultiBodyCreator::createCollisionObjectGraphicsInstance2(int linkIndex, class btCollisionObject* col, const btVector4& colorRgba, const btVector3& specularColor)
{
	createCollisionObjectGraphicsInstance(linkIndex, col, colorRgba);
	int graphicsInstanceId = col->getUserIndex();
	btVector3DoubleData speculard;
	specularColor.serializeDouble(speculard);
	m_guiHelper->changeSpecularColor(graphicsInstanceId, speculard.m_floats);
}

btMultiBody* MyMultiBodyCreator::getBulletMultiBody()
{
	return m_bulletMultiBody;
}
