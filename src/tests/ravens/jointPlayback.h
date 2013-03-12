#ifndef __JOINT_PLAYBACK__
#define __JOINT_PLAYBACK__

#include <sstream>
#include <string>

#include "jointLFDProcessor.h"

using namespace std;

class CustomScene;

/* Class to load a trajectory and play it back. **/
class jointPlayback {

	CustomScene &scene;								// Scene in which robot is

	vector<int> joint_inds;		 					// Indices of joints to set DOF values
	vector<dReal> joint_vals;						// Latest joint values loaded from file.

	LFDProcessor::Ptr lfdProcessor;					// Processor for LFD
	vector <vector <double> > processedJoints;		// If doing LFD Processing
	bool processedSuccessfully;						// LFD processing was successful
	unsigned int jCount, gCount, rCount;			// Counters for joints, grabs and releases
	bool initialized;								// LFD Processor intialized?

	float freq;										// Frequency of recording
	float dt;										// Time step of simulation
	float currTime;									// Amount of time passed since last execution

	string filename;								// Name of file to read from
	ifstream file;									// Input file stream to read file

	bool enabled;									// Check if trajectory is currently playing back
	bool file_closed;								// Check if the file is closed
	bool processing;

public:

	typedef boost::shared_ptr<jointPlayback> Ptr;

	// Constructor
	jointPlayback (	CustomScene &_scene, bool _processing=false,
					float _freq = -1.0, float _dt = -1.0,
					string _filename="/home/sibi/sandbox/bulletsim/src/tests/ravens/recorded/raven_joints.txt");

	/** Start the controller. */
	void run() {
		enabled = true;
		if (!processing && file_closed) {
			file.open(filename.c_str(), ios::in);
			file_closed = false;
		} else if (processing) {
			lfdProcessor->initProcessing();
			initialized = true;
			process();
		}
	}

	/* Halt the controller.
	 * Controller does not move the robot until run/ resume is called. */
	void halt() {enabled = false;}

	/** Resume the operation of the controller after a halt.*/
	void resume() {run();}

	// Goes without saying what this does
	void toggleEnabled () {
		if (enabled) halt();
		else run();

		std::cout<<"Playback: "<<(enabled ? "true" : "false") << std::endl;
	}

	// Callback to execute waypoints from file
	void executeNextWaypoint ();

	// Process trajectory using LFD
	void process ();

	// Callback to play processed points
	void playProcessed ();

	~jointPlayback() {file.close();}

};

#endif
