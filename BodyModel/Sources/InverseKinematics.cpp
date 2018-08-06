#include "pch.h"
#include "InverseKinematics.h"
#include "Settings.h"

#include <Kore/Log.h>

InverseKinematics::InverseKinematics(std::vector<BoneNode*> boneVec) {
	bones = boneVec;
	setJointConstraints();
	
	totalNum = 0;
	evalReached = 0;
	
	// iterations
	evalIterations[0] = 0;
	evalIterations[1] = FLT_MAX;
	evalIterations[2] = FLT_MIN;
	
	// pos-error
	evalErrorPos[0] = 0;
	evalErrorPos[1] = FLT_MAX;
	evalErrorPos[2] = FLT_MIN;
	
	// rot-error
	evalErrorRot[0] = 0;
	evalErrorRot[1] = FLT_MAX;
	evalErrorRot[2] = FLT_MIN;
	
	// time
	evalTime[0] = 0;
	evalTime[1] = FLT_MAX;
	evalTime[2] = FLT_MIN;
	
	// time per iteration
	evalTimeIteration[0] = 0;
	evalTimeIteration[1] = FLT_MAX;
	evalTimeIteration[2] = FLT_MIN;
}

void InverseKinematics::inverseKinematics(BoneNode* targetBone, Kore::vec3 desPosition, Kore::Quaternion desRotation) {
	std::vector<float> deltaTheta, prevDeltaTheta;
	float errorPos = -1.0f;
	float errorRot = -1.0f;
	bool stucked = false;
	struct timespec start, end;
	struct timespec start2, end2;
	
	if (eval) clock_gettime(CLOCK_MONOTONIC_RAW, &start);
	
	int i = 0;
	// while position not reached and maxStep not reached and not stucked
	while ((errorPos < 0 || errorPos > errorMaxPos || errorRot < 0 || errorRot > errorMaxRot) && i < maxSteps && !stucked) {
		if (eval) clock_gettime(CLOCK_MONOTONIC_RAW, &start2);
		
		prevDeltaTheta = deltaTheta;
		
		// todo: better!
		if (targetBone->nodeIndex == tracker[0]->boneIndex || targetBone->nodeIndex == tracker[1]->boneIndex) {
			deltaTheta = jacobianHand->calcDeltaTheta(targetBone, desPosition, desRotation, tracker[0]->ikMode);
			errorPos = jacobianHand->getPositionError();
			errorRot = jacobianHand->getRotationError();
		} else if (targetBone->nodeIndex == tracker[3]->boneIndex || targetBone->nodeIndex == tracker[4]->boneIndex) {
			deltaTheta = jacobianFoot->calcDeltaTheta(targetBone, desPosition, desRotation, tracker[3]->ikMode);
			errorPos = jacobianFoot->getPositionError();
			errorRot = jacobianFoot->getRotationError();
		}
		
		// check if ik stucked (runned in extrema)
		if (i) {
			float sum = 0;
			int j = 0;
			while (!stucked && j < prevDeltaTheta.size()) {
				sum += fabs(prevDeltaTheta[j] - deltaTheta[j]);
				j++;
			}
			stucked = sum < nearNull;
		}
		
		applyChanges(deltaTheta, targetBone);
		applyJointConstraints(targetBone);
		for (int i = 0; i < bones.size(); ++i)
			updateBone(bones[i]);
		
		if (eval && i == 0) {
			// time per iteration
			clock_gettime(CLOCK_MONOTONIC_RAW, &end2);
			float time = (end2.tv_sec - start2.tv_sec) * 1000000 + (end2.tv_nsec - start2.tv_nsec) / 1000;
			evalTimeIteration[0] += time;
			evalTimeIteration[1] = time < evalTimeIteration[1] ? time : evalTimeIteration[1];
			evalTimeIteration[2] = time > evalTimeIteration[2] ? time : evalTimeIteration[2];
		}
		
		i++;
	}
	
	if (eval) {
		clock_gettime(CLOCK_MONOTONIC_RAW, &end);
		
		totalNum += 1;
		evalReached += (errorPos < errorMaxPos && errorRot < errorMaxRot) ? 1 : 0;
		
		// iterations
		evalIterations[0] += i;
		evalIterations[1] = i < evalIterations[1] ? (float) i : evalIterations[1];
		evalIterations[2] = i > evalIterations[2] ? (float) i : evalIterations[2];
		
		// pos-error
		evalErrorPos[0] += errorPos > 0 ? errorPos : 0;
		evalErrorPos[1] = errorPos < evalErrorPos[1] ? errorPos : evalErrorPos[1];
		evalErrorPos[2] = errorPos > evalErrorPos[2] ? errorPos : evalErrorPos[2];
		
		// rot-error
		evalErrorRot[0] += errorRot > 0 ? errorRot : 0;
		evalErrorRot[1] = errorRot < evalErrorRot[1] ? errorRot : evalErrorRot[1];
		evalErrorRot[2] = errorRot > evalErrorRot[2] ? errorRot : evalErrorRot[2];
		
		// time
		float time = (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_nsec - start.tv_nsec) / 1000;
		evalTime[0] += time;
		evalTime[1] = time < evalTime[1] ? time : evalTime[1];
		evalTime[2] = time > evalTime[2] ? time : evalTime[2];
	}
}

void InverseKinematics::updateBone(BoneNode* bone) {
	if (bone->parent->initialized) {
		bone->combined = bone->parent->combined * bone->local;
	}
}

void InverseKinematics::initializeBone(BoneNode* bone) {
	updateBone(bone);
	
	if (!bone->initialized) {
		bone->initialized = true;
		bone->combinedInv = bone->combined.Invert();
	}
	
	bone->finalTransform = bone->combined * bone->combinedInv;
}

void InverseKinematics::applyChanges(std::vector<float> deltaTheta, BoneNode* targetBone) {
	unsigned long size = deltaTheta.size();
	int i = 0;
	
	BoneNode* bone = targetBone;
	while (bone->initialized && i < size) {
		Kore::vec3 axes = bone->axes;
		
		if (axes.x() == 1.0 && i < size) bone->quaternion.rotate(Kore::Quaternion(Kore::vec3(1, 0, 0), deltaTheta[i++]));
		if (axes.y() == 1.0 && i < size) bone->quaternion.rotate(Kore::Quaternion(Kore::vec3(0, 1, 0), deltaTheta[i++]));
		if (axes.z() == 1.0 && i < size) bone->quaternion.rotate(Kore::Quaternion(Kore::vec3(0, 0, 1), deltaTheta[i++]));
		
		bone->quaternion.normalize();
		bone->local = bone->transform * bone->quaternion.matrix().Transpose();
		
		bone = bone->parent;
	}
}

void InverseKinematics::applyJointConstraints(BoneNode* targetBone) {
	BoneNode* bone = targetBone;
	while (bone->initialized) {
		Kore::vec3 axes = bone->axes;
		
		Kore::vec3 rot;
		Kore::RotationUtility::quatToEuler(&bone->quaternion, &rot.x(), &rot.y(), &rot.z());
		
		float x = (axes.x() == 1.0) ? clampValue(bone->constrain[0].x(), bone->constrain[0].y(), rot.x()) : rot.x();
		float y = (axes.y() == 1.0) ? clampValue(bone->constrain[0].x(), bone->constrain[0].y(), rot.y()) : rot.y();
		float z = (axes.z() == 1.0) ? clampValue(bone->constrain[0].x(), bone->constrain[0].y(), rot.z()) : rot.z();
		
		Kore::RotationUtility::eulerToQuat(x, y, z, &bone->quaternion);
		
		// bone->quaternion = Kore::Quaternion((double) x, (double) y, (double) z, 1);
		bone->quaternion.normalize();
		bone->local = bone->transform * bone->quaternion.matrix().Transpose();
		
		bone = bone->parent;
	}
}

float InverseKinematics::clampValue(float minVal, float maxVal, float value) {
	if (minVal > maxVal) {
		float temp = minVal;
		minVal = maxVal;
		maxVal = temp;
	}
	
	if (value < minVal)
		return minVal;
	else if (value > maxVal)
		return maxVal;
	
	return value;
}

void InverseKinematics::setJointConstraints() {
	BoneNode* nodeLeft;
	BoneNode* nodeRight;
	
	// upperarm / Schultergelenk
	nodeLeft = bones[12 - 1];
	nodeLeft->axes = Kore::vec3(1, 1, 1);
	nodeLeft->constrain.push_back(Kore::vec2(-5.0f * Kore::pi / 18.0f, Kore::pi));                  // -50° bis 180° = 230° (LH, vorher -90° bis 120° = 210° => -20°)
	nodeLeft->constrain.push_back(Kore::vec2(-Kore::pi / 2.0f, Kore::pi / 2.0f));                   // -90° bis 90° = 180° (LH, vorher -90° bis 60° = 150° => 30°)
	nodeLeft->constrain.push_back(Kore::vec2(-13.0f * Kore::pi / 18.0f, Kore::pi / 2.0f));          // -130° bis 90° = 220° (NN, vorher -30° bis 120° = 150° => 70°)
	
	nodeRight = bones[22 - 1];
	nodeRight->axes = nodeLeft->axes;
	nodeRight->constrain.push_back(nodeLeft->constrain[0]);
	nodeRight->constrain.push_back(nodeLeft->constrain[1] * -1.0f);
	nodeRight->constrain.push_back(nodeLeft->constrain[2] * -1.0f);
	
	// lowerarm / Ellenbogengelenk
	nodeLeft = bones[13 - 1];
	nodeLeft->axes = Kore::vec3(1, 0, 0);
	nodeLeft->constrain.push_back(Kore::vec2(0, 7.0f * Kore::pi / 9.0f));           				// 0° bis 140° = 150° (LH, vorher -90° bis 90° = 180° => -30°)
	
	nodeRight = bones[23 - 1];
	nodeRight->axes = nodeLeft->axes;
	nodeRight->constrain.push_back(nodeLeft->constrain[0]);
	
	// hand
	nodeLeft = bones[14 - 1];
	nodeLeft->axes = Kore::vec3(1, 0, 1);
	nodeLeft->constrain.push_back(Kore::vec2(-2.0f * Kore::pi / 9.0f, Kore::pi / 6.0f));        // -40° bis 30° = 75° (NN)
	nodeLeft->constrain.push_back(Kore::vec2(-7.0f * Kore::pi / 18.0f, Kore::pi / 3.0f));       // -70° bis 60° = 130° (NN)
	
	nodeRight = bones[24 - 1];
	nodeRight->axes = nodeLeft->axes;
	nodeRight->constrain.push_back(nodeLeft->constrain[0]);
	nodeRight->constrain.push_back(nodeLeft->constrain[1] * -1.0f);
	
	// thigh / Hüftgelenk
	nodeLeft = bones[4 - 1];
	nodeLeft->axes = Kore::vec3(1, 1, 1);
	nodeLeft->constrain.push_back(Kore::vec2(-13.0f * Kore::pi / 18.0f, Kore::pi / 6.0f));          // -130° bis 30° = 160° (NN/LH, vorher -150° bis 60° = 210° => -50°)
	nodeLeft->constrain.push_back(Kore::vec2(-Kore::pi / 3.0f, 2.0f * Kore::pi / 9.0f));            // -60° bis 40° = 100° (NN, vorher -22.5° bis 22.5° = 45° => 55°)
	nodeLeft->constrain.push_back(Kore::vec2(-5.0f * Kore::pi / 18.0f, 5.0f * Kore::pi / 18.0f));   // -50° bis 50° = 100° (LH/NN, vorher -90° bis 90° = 180° => -80°)
	
	nodeRight = bones[29 - 1];
	nodeRight->axes = nodeLeft->axes;
	nodeRight->constrain.push_back(nodeLeft->constrain[0]);
	nodeRight->constrain.push_back(nodeLeft->constrain[1] * -1.0f);
	nodeRight->constrain.push_back(nodeLeft->constrain[2] * -1.0f);
	
	// calf / Kniegelenk
	nodeLeft = bones[5 - 1];
	nodeLeft->axes = Kore::vec3(1, 0, 0);
	nodeLeft->constrain.push_back(Kore::vec2(0, 7.0f * Kore::pi / 9.0f));           				// 0° bis 140° = 150° (LH, vorher 0° bis 150° = 150° => 0°)
	
	nodeRight = bones[30 - 1];
	nodeRight->axes = nodeLeft->axes;
	nodeRight->constrain.push_back(nodeLeft->constrain[0]);
}

float InverseKinematics::getReached() {
	return totalNum != 0 ? (float) evalReached / (float) totalNum : -1;
}

float* InverseKinematics::getIterations() {
	evalIterations[0] = totalNum != 0 ? (float) evalIterations[0] / (float) totalNum : -1;
	
	return evalIterations;
}

float* InverseKinematics::getErrorPos() {
	evalErrorPos[0] = totalNum != 0 ? evalErrorPos[0] / (float) totalNum : -1;
	
	return evalErrorPos;
}

float* InverseKinematics::getErrorRot() {
	evalErrorRot[0] = totalNum != 0 ? evalErrorRot[0] / (float) totalNum : -1;
	
	return evalErrorRot;
}

float* InverseKinematics::getTime() {
	evalTime[0] = totalNum != 0 ? evalTime[0] / (float) totalNum : -1;
	
	return evalTime;
}

float* InverseKinematics::getTimeIteration() {
	evalTimeIteration[0] = totalNum != 0 ? evalTimeIteration[0] / (float) totalNum : -1;
	
	return evalTimeIteration;
}
