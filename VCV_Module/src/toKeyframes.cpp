#include "plugin.hpp"
#include <patch.hpp>

// Function to convert a vector of floats to a CSV string
void saveKeyframesToCSV(const std::vector<std::vector<float>>& kyfrms, const std::string filePath) {
	
	if(kyfrms.size() == 0) {
		return;
	}

	std::ostringstream csvStream;

	// ADD TITLES TO COLUMNS
	// for (size_t track = 0; track < kyfrms[0].size(); track++) {
	// 	csvStream << "track_" + std::to_string(track);
	// 	if(track != kyfrms[0].size() - 1){
	// 		csvStream << ",";
	// 	}
	// }
	// csvStream << "\n";

	// Make a row out of each keyframe
	for (size_t frame = 0; frame < kyfrms.size(); frame++) {
		for (size_t track = 0; track < kyfrms[0].size(); track++) {
			csvStream << std::to_string( kyfrms[frame][track] );
			if(track < kyfrms[0].size() - 1){
				csvStream << ",";
			}
		}

		csvStream << "\n";
	}

	// Create or open the CSV file and write the CSV data
	std::ofstream csvFile(filePath);
	if (csvFile.is_open()) {
		csvFile << csvStream.str();
		csvFile.close();
	}

	return;
}

struct ToKeyframes : Module {

	// TODO: connect the keyframeRate up with the appropriate param input
	int64_t keyframeRate = 24; // stored in this data type so that there is less casting per process call
	int64_t startFrame = 0; // this value is set when the RECORD input is triggered
	bool recordingActive = false; // determines whether keyframes will be added
	// each row is a keyframe, each column is a track. Makes it easier to append values (and prob better for mem management)
	// this means there will be 16 columns, one for each track
	std::vector<std::vector<float>> keyframes;
	
	float prevSaveVoltage  = 0.f;
	float prevStartVoltage = 0.f;
	float prevStopVoltage  = 0.f;

	float input_9_avg  = 0.f;
	float input_10_avg = 0.f;
	float input_11_avg = 0.f;
	float input_12_avg = 0.f;

	// Determines the resolution of a visualized waveform
	// TODO: connect the waveformResolution up with the appropriate param input
	const int64_t waveformResolution = 64;

    // Set the dimensions
    int numWfs = 5;

	// Define a 2D vector representing the current state of all the waveforms
    std::vector<std::vector<float>> currWaveformState = std::vector<std::vector<float>> (
		numWfs, 
		std::vector<float> (waveformResolution, 0.f)
	);

	// Define a 3D vector representing the visual keyframes of the waves over time
    std::vector<std::vector<std::vector<float>>> waveformKeyframes = std::vector<std::vector<std::vector<float>>>(
		numWfs, 
		std::vector<std::vector<float>>(
			waveformResolution,
			std::vector<float>(waveformResolution, 0.f)
		)
	);

	enum ParamId {
		FRAME_RATE_PARAM,
		WAVE_SAMPLE_RATE_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		START_INPUT,
		ABORT_INPUT,
		SAVE_INPUT,
		WAVE_I_INPUT,
		WAVE_II_INPUT,
		WAVE_III_INPUT,
		WAVE_IV_INPUT,
		WAVE_V_INPUT,
		VOCT_I_INPUT,
		VOCT_II_INPUT,
		VOCT_III_INPUT,
		VOCT_IV_INPUT,
		VOCT_V_INPUT,
		INPUT_1_INPUT,
		INPUT_2_INPUT,
		INPUT_3_INPUT,
		INPUT_4_INPUT,
		INPUT_5_INPUT,
		INPUT_6_INPUT,
		INPUT_7_INPUT,
		INPUT_8_INPUT,
		INPUT_9_INPUT,
		INPUT_10_INPUT,
		INPUT_11_INPUT,
		INPUT_12_INPUT,
		INPUT_13_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		OUTPUTS_LEN
	};
	enum LightId {
		LIGHTS_LEN
	};

	ToKeyframes() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(FRAME_RATE_PARAM, 0.f, 1.f, 0.f, "");
		configParam(WAVE_SAMPLE_RATE_PARAM, 0.f, 1.f, 0.f, "");
		configInput(START_INPUT, "");
		configInput(ABORT_INPUT, "");
		configInput(SAVE_INPUT, "");
		configInput(WAVE_I_INPUT, "");
		configInput(WAVE_II_INPUT, "");
		configInput(WAVE_III_INPUT, "");
		configInput(WAVE_IV_INPUT, "");
		configInput(WAVE_V_INPUT, "");
		configInput(VOCT_I_INPUT, "");
		configInput(VOCT_II_INPUT, "");
		configInput(VOCT_III_INPUT, "");
		configInput(VOCT_IV_INPUT, "");
		configInput(VOCT_V_INPUT, "");
		configInput(INPUT_1_INPUT, "");
		configInput(INPUT_2_INPUT, "");
		configInput(INPUT_3_INPUT, "");
		configInput(INPUT_4_INPUT, "");
		configInput(INPUT_5_INPUT, "");
		configInput(INPUT_6_INPUT, "");
		configInput(INPUT_7_INPUT, "");
		configInput(INPUT_8_INPUT, "");
		configInput(INPUT_9_INPUT, "");
		configInput(INPUT_10_INPUT, "");
		configInput(INPUT_11_INPUT, "");
		configInput(INPUT_12_INPUT, "");
		configInput(INPUT_13_INPUT, "");
	}

	// resets the average values of the inputs that record keyframes of the average value rather than a straight up sample
	// resets the recorded waveforms to zero
	void resetCurrKfData(){
		input_9_avg  = 0.0;
		input_10_avg = 0.0;
		input_11_avg = 0.0;
		input_12_avg = 0.0;

		for(int currWave = 0; currWave < numWfs; currWave++){
			currWaveformState[currWave] = {std::vector<float>(waveformResolution, 0.f)};
		}
	}

	void clearStoredKfs(){
		keyframes.clear();

		waveformKeyframes.clear();
	}

	void saveKfsToDisk(std::string parent_folder){
		// TODO: maybe spawn one async thread that saves all these files
		// TODO: pass by ref and then make sure kf data is not erased or manipulated until done instead of pass by value
		std::async(saveKeyframesToCSV, keyframes, parent_folder + "keyframes.csv");
		
		for(int currWave = 0; currWave < numWfs; currWave++){
			std::async(saveKeyframesToCSV, waveformKeyframes[currWave], parent_folder + "waveform_" + std::to_string(currWave) + "_keyframes.csv");	
		}	
	}

	void process(const ProcessArgs& args) override {

		// activate recording if the RECORD input is triggered
		if(prevStartVoltage == 0.f && inputs[START_INPUT].getVoltage() > prevStartVoltage){
			recordingActive = true;
			startFrame = args.frame;
		}
		prevStartVoltage = inputs[START_INPUT].getVoltage();

		// deactivate recording and clear keyframes of the ABORT input is triggered
		if(prevStopVoltage == 0.f && inputs[ABORT_INPUT].getVoltage() > prevStopVoltage){
			clearStoredKfs();
			resetCurrKfData();

			recordingActive = false;
		}
		prevStopVoltage = inputs[ABORT_INPUT].getVoltage();
		
		if(recordingActive) {
			// TODO: maybe this should be calles "samplesInKeyframe" for understandability
			int64_t framesInKeyframe = (int64_t)args.sampleRate / keyframeRate; // calculated every frame bc sample rate can change during execution
			int64_t currFrameInKf = (args.frame - startFrame) % framesInKeyframe;
			int fractionOfAvg = ( 1.0 / float(framesInKeyframe) );
			
			// accumulate what will be the eventual average value of inputs 9-12
			input_9_avg  += fractionOfAvg *  inputs[INPUT_9_INPUT].getVoltage();
			input_10_avg += fractionOfAvg * inputs[INPUT_10_INPUT].getVoltage();
			input_11_avg += fractionOfAvg * inputs[INPUT_11_INPUT].getVoltage();
			input_12_avg += fractionOfAvg * inputs[INPUT_12_INPUT].getVoltage();

			// modify the current waveform keyframe
			int64_t framesInWfSample = (framesInKeyframe / waveformResolution);
			int64_t currWfSample = currFrameInKf / framesInWfSample; 

			// TODO: associate the construction of the waveform keyframes with their associated v/oct inputs
			// currWfKeyframe_II[currWfSample]  += (1.0 / float(framesInWfSample)) * inputs[WAVE_II_INPUT].getVoltage();
			// currWfKeyframe_III[currWfSample] += (1.0 / float(framesInWfSample)) * inputs[WAVE_III_INPUT].getVoltage();
			// currWfKeyframe_IV[currWfSample]  += (1.0 / float(framesInWfSample)) * inputs[WAVE_IV_INPUT].getVoltage();
			// currWfKeyframe_V[currWfSample]   += (1.0 / float(framesInWfSample)) * inputs[WAVE_V_INPUT].getVoltage();

			// use v/oct to capture a window if the signal the length of 1 wavelength
			float hz = (440.f / 2.f) * pow(2.f, (inputs[VOCT_I_INPUT].getVoltage() + 0.25)); // convert from v/oct to hertz
			float samplesInWavelength = args.sampleRate / hz; // determine the number of audio samples in one wavelength of this wave

			if (
				// if the wavelength is less than the freq of the visual keyframe, capture the latest full wavelength in the visual keyframe
			 	(
				 (args.frame % (int64_t)samplesInWavelength) == 0 &&
			 	 float(framesInKeyframe - currFrameInKf) < (samplesInWavelength * 2.f) &&
			 	 float(framesInKeyframe - currFrameInKf) > samplesInWavelength
				)
				||
				// if the wavelength is longer than the frequency of the visual frame rate, use different conditions
				(
				 samplesInWavelength > framesInKeyframe && 
				 args.frame % (int64_t)samplesInWavelength == 0
				)
			){
			 	// DEBUG("		wave");
				// DEBUG("		v/oct input: %f", inputs[VOCT_I_INPUT].getVoltage());
				// DEBUG("		hz: %f", hz);
				// DEBUG("		sample rate: %f", args.sampleRate);
				// DEBUG("		samples in wavelength: %f", samplesInWavelength);
				// DEBUG("		current frame within keyframe: %ld", currFrameInKf);
				// DEBUG("		total frames within keyframe: %ld", framesInKeyframe);
			}
			// currWfKeyframe_I[currWfSample]   += (1.0 / float(framesInWfSample)) * inputs[WAVE_I_INPUT].getVoltage();
			
			// if it is determined that this is the last audio frame of the keyframe, save the values to the list of keyframes 
			if(currFrameInKf == 0){
				// output the value of the keyframe
				// converts this frame to a value between 0..1 depending on the frame it is in this second 1/24, 2/24, ...
				//outputs[FRAME_START_OUTPUT].setVoltage( (float)((args.frame / framesInKeyframe) % keyframeRate) / (float)(keyframeRate) );
				
				std::vector<float> thisKeyframe = {
					inputs[INPUT_1_INPUT].getVoltage(),
					inputs[INPUT_2_INPUT].getVoltage(),
					inputs[INPUT_3_INPUT].getVoltage(),
					inputs[INPUT_4_INPUT].getVoltage(),
					inputs[INPUT_5_INPUT].getVoltage(),
					inputs[INPUT_6_INPUT].getVoltage(),
					inputs[INPUT_7_INPUT].getVoltage(),
					inputs[INPUT_8_INPUT].getVoltage(),
					input_9_avg,
					input_10_avg,
					input_11_avg,
					input_12_avg,
					inputs[INPUT_13_INPUT].getVoltage(),
				};

				keyframes.push_back(thisKeyframe);

				waveformKeyframes.push_back(currWaveformState);					

				// prepare for a new keyframe
				resetCurrKfData();

				DEBUG("number of keyframes is %ld", keyframes.size());

			}
		}

		// asynchronously save the keyframes to disk as a CSV file upon trigger
		if(prevSaveVoltage == 0.f && inputs[SAVE_INPUT].getVoltage() > prevSaveVoltage){
			DEBUG("Saving Keyframes... ");
			auto parent_folder = APP->patch->path.substr(0, APP->patch->path.rfind("/") + 1);
			DEBUG("Saving Data to \"%s\"", parent_folder.c_str());

			DEBUG("rows in keyframes %ld", keyframes.size());
			if( keyframes.size() > 0 ){
				DEBUG("cols in keyframes %ld", keyframes[0].size());
			}
			
			saveKfsToDisk(parent_folder);
			
			clearStoredKfs();
			resetCurrKfData();
			
			recordingActive = false;
		}
		prevSaveVoltage = inputs[SAVE_INPUT].getVoltage();

		
	}
};


struct ToKeyframesWidget : ModuleWidget {
	BGPanel *pBackPanel;
	
	ToKeyframesWidget(ToKeyframes* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/ToKeyframes.svg")));

		// Adapted from https://github.com/netboy3/MSM-vcvrack-plugin/
		pBackPanel = new BGPanel();
		pBackPanel->box.size = box.size;
		pBackPanel->imagePath = asset::plugin(pluginInstance, "res/ToKeyframes.jpg");
		pBackPanel->visible = false;
		addChild(pBackPanel);
		pBackPanel->visible = true;

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(153.479, 23.874)), module, ToKeyframes::FRAME_RATE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(160.08, 49.694)), module, ToKeyframes::WAVE_SAMPLE_RATE_PARAM));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(153.479, 23.874)), module, ToKeyframes::FRAME_RATE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(160.08, 49.694)), module, ToKeyframes::WAVE_SAMPLE_RATE_PARAM));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(28.534, 13.424)), module, ToKeyframes::START_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(59.936, 13.354)), module, ToKeyframes::ABORT_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(90.878, 19.109)), module, ToKeyframes::SAVE_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(39.611, 45.975)), module, ToKeyframes::INPUT_2_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(106.828, 46.561)), module, ToKeyframes::WAVE_I_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(62.871, 47.059)), module, ToKeyframes::INPUT_3_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(16.502, 49.044)), module, ToKeyframes::INPUT_1_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(134.556, 49.003)), module, ToKeyframes::WAVE_II_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(85.815, 51.288)), module, ToKeyframes::INPUT_4_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(115.716, 56.7)), module, ToKeyframes::VOCT_I_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(143.609, 59.145)), module, ToKeyframes::VOCT_II_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(53.596, 68.292)), module, ToKeyframes::INPUT_6_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(30.335, 68.777)), module, ToKeyframes::INPUT_5_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(95.721, 71.242)), module, ToKeyframes::WAVE_III_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(76.645, 71.479)), module, ToKeyframes::INPUT_7_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(123.479, 74.974)), module, ToKeyframes::WAVE_IV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(151.449, 75.38)), module, ToKeyframes::WAVE_V_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(104.727, 81.414)), module, ToKeyframes::VOCT_III_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(132.461, 85.203)), module, ToKeyframes::VOCT_IV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(160.423, 85.545)), module, ToKeyframes::VOCT_V_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(39.608, 90.196)), module, ToKeyframes::INPUT_9_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(62.837, 91.32)), module, ToKeyframes::INPUT_10_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(16.532, 93.388)), module, ToKeyframes::INPUT_8_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(85.809, 95.59)), module, ToKeyframes::INPUT_11_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(108.676, 100.108)), module, ToKeyframes::INPUT_12_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(131.844, 102.18)), module, ToKeyframes::INPUT_13_INPUT));
	}
};


Model* modelToKeyframes = createModel<ToKeyframes, ToKeyframesWidget>("ToKeyframes");