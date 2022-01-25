#include "api/echo_canceller3_factory.h"
#include "api/echo_canceller3_config.h"
#include "audio_processing/include/audio_processing.h"
#include "audio_processing/audio_buffer.h"
#include "audio_processing/high_pass_filter.h"

#include "wavreader.h"
#include "wavwriter.h"

#include <iostream>

using namespace webrtc;
using namespace std;

void print_wav_information(const char* fn, int format, int channels, int sample_rate, int bits_per_sample, int length)
{
	cout << "=====================================" << endl
		<< fn << " information:" << endl
		<< "format: " << format << endl
		<< "channels: " << channels << endl
		<< "sample_rate: " << sample_rate << endl
		<< "bits_per_sample: " << bits_per_sample << endl
		<< "length: " << length << endl
		<< "total_samples: " << length / bits_per_sample * 8 << endl
		<< "======================================" << endl;
}

float calculate_power_dB(void* audioPtr, unsigned int audioDataLength, int voiceBitsPerSample, int sampleRate, bool lastSecondOnly)
{
	if (voiceBitsPerSample != 16)
	{
		cerr << "calculate_wav_power_dB only accepts 16 bit samples" << endl;
		return -1; 
	}

	unsigned char* audioData = new unsigned char[audioDataLength];
	wav_read_data(audioPtr, audioData, audioDataLength);
	int16_t* audioData16 = reinterpret_cast<int16_t*>(audioData);
	int voiceSamples = audioDataLength * 8 / voiceBitsPerSample;
	float voiceEnergy = 0;
	
	for (int i = lastSecondOnly? voiceSamples - sampleRate : 0; i < voiceSamples; i++)
	{
		voiceEnergy += static_cast<float>(audioData16[i] * audioData16[i]);
	}
	return 10*log10(voiceEnergy / voiceSamples);
}

float calculate_wav_power_dB (char *wavFilename, bool lastSecondOnly)
{
	void *h;
	int format, channels, sample_rate, bits_per_sample;
	unsigned int voice_data_length, data_length;
	h = wav_read_open(wavFilename);
	int res = wav_get_header(h, &format, &channels, &sample_rate, &bits_per_sample, &data_length);
	if (!res)
	{
		cerr << "calculate_wav_power_dB: get header error: " << res << endl;
		return -1;
	}
	return calculate_power_dB(h, data_length, bits_per_sample, sample_rate, lastSecondOnly);
}

bool check_KPIs(int numAudioFiles, char* audioFilenames[])
{
	if (numAudioFiles != 3)
	{
		cerr << "check_KPIs - Error: needs 3 input audio files" << endl;
	}
	cout << "======================================" << endl
	<< "ref file is: " << audioFilenames[0] << endl
	<< "rec file is: " << audioFilenames[1] << endl
	<< "out file is: " << audioFilenames[2] << endl
	<< "voice file is: " << audioFilenames[3] << endl
	<< "======================================" << endl;

	float gameSoundPower_dB = calculate_wav_power_dB(audioFilenames[1], true); // last second of rec file
	float residualNoisePower_dB = calculate_wav_power_dB(audioFilenames[2], true); // last second of out file
	float voicePower_dB = calculate_wav_power_dB(audioFilenames[3], false); // whole voice file
	float outPower_dB = calculate_wav_power_dB(audioFilenames[2], false); // whole out file

	// Thresholds to pass the tests
	const float MAXIMUM_VOICE_ATTENUATION = 5; //dB
	const float MINIMUM_GAME_SOUND_ATTENUATION = 20; // dB

	bool result = true;
	if (outPower_dB < voicePower_dB - MAXIMUM_VOICE_ATTENUATION)
	{
		cerr << "check_KPIs Error: outPower_dB < voicePower_dB - MAXIMUM_VOICE_ATTENUATION" << endl;
		result = false;
	}	
	if (residualNoisePower_dB > gameSoundPower_dB - MINIMUM_GAME_SOUND_ATTENUATION)
	{
		cerr << "check_KPIs Error: residualNoisePower_dB > gameSoundPower_dB - MINIMUM_GAME_SOUND_ATTENUATION" << endl;
		result = false;
	}	
	return result;
}

void print_progress(int current, int total)
{
	int percentage = current / static_cast<float>(total) * 100;
	static constexpr int p_bar_length = 50;
	int progress = percentage * p_bar_length / 100;
	cout << "        " << current << "/" << total << "    " << percentage << "%";
	cout << "|";
	for (int i = 0; i < progress; ++i)
		cout << "=";
	cout << ">";
	for (int i = progress; i < p_bar_length; ++i)
		cout << " ";
	cout << "|";
	cout <<"\r";
}

int main(int argc, char* argv[])
{
	bool CHECK_KPIS = false;

	if (argc != 4 && argc != 5)
	{
		cerr << endl;
		cerr << "usage for just generating AEC output audio file" << endl; 
		cerr << "./demo ref.wav rec.wav out.wav" << endl;
		cerr << endl;
		cerr << "usage for testing KPIs." << endl;
		cerr << "./demo ref.wav rec.wav out.wav voice.wav" << endl;
		cerr << "Make sure:" << endl;
		cerr << "- rec.wav is at least 1 second longer than voice.wav" << endl;
		cerr << "- voice.wav is exactly the same audio used to create rec.wav (volume must be the same)" << endl;

		cerr << endl;
		return -1;
	}

	void *h_ref, *h_rec, *h_voice;
	
	cout << "======================================" << endl
	<< "ref file is: " << argv[1] << endl
	<< "rec file is: " << argv[2] << endl
	<< "out file is: " << argv[3] << endl
	<< "======================================" << endl;
	h_ref = wav_read_open(argv[1]);
	h_rec = wav_read_open(argv[2]);
	
	if (argc == 5)
	{
		cout << "voice file is: " << argv[4] << endl;
		h_voice = wav_read_open(argv[4]);
		CHECK_KPIS = true;
	}

	int ref_format, ref_channels, ref_sample_rate, ref_bits_per_sample;
	int rec_format, rec_channels, rec_sample_rate, rec_bits_per_sample;
	unsigned int ref_data_length, rec_data_length;

	int res = wav_get_header(h_ref, &ref_format, &ref_channels, &ref_sample_rate, &ref_bits_per_sample, &ref_data_length);
	if (!res)
	{
		cerr << "get ref header error: " << res << endl;
		return -1;
	}
	int ref_samples = ref_data_length * 8 / ref_bits_per_sample;
	print_wav_information(argv[1], ref_format, ref_channels, ref_sample_rate, ref_bits_per_sample, ref_data_length);
	
	res = wav_get_header(h_rec, &rec_format, &rec_channels, &rec_sample_rate, &rec_bits_per_sample, &rec_data_length);
	if (!res)
	{
		cerr << "get rec header error: " << res << endl;
		return -1;
	}
	int rec_samples = rec_data_length * 8 / rec_bits_per_sample;
	print_wav_information(argv[2], rec_format, rec_channels, rec_sample_rate, rec_bits_per_sample, rec_data_length);
	
	if (ref_format != rec_format ||
		ref_channels != rec_channels ||
		ref_sample_rate != rec_sample_rate ||
		ref_bits_per_sample != rec_bits_per_sample) 
	{
		cerr << "ref file format != rec file format" << endl;
		return -1;
	}

	if (CHECK_KPIS)
	{
		int voice_format, voice_channels, voice_sample_rate, voice_bits_per_sample;
		unsigned int voice_data_length;
		res = wav_get_header(h_voice, &voice_format, &voice_channels, &voice_sample_rate, &voice_bits_per_sample, &voice_data_length);
		if (!res)
		{
			cerr << "get voice header error: " << res << endl;
			return -1;
		}
		print_wav_information(argv[4], voice_format, voice_channels, voice_sample_rate, voice_bits_per_sample, voice_data_length);
	}
	
	EchoCanceller3Config aec_config;
	aec_config.filter.export_linear_aec_output = true;
	EchoCanceller3Factory aec_factory = EchoCanceller3Factory(aec_config);
	std::unique_ptr<EchoControl> echo_controller = aec_factory.Create(ref_sample_rate, ref_channels, rec_channels);
	std::unique_ptr<HighPassFilter> hp_filter = std::make_unique<HighPassFilter>(rec_sample_rate, rec_channels);

	int sample_rate = rec_sample_rate;
	int channels = rec_channels;
	int bits_per_sample = rec_bits_per_sample;
	StreamConfig config = StreamConfig(sample_rate, channels, false);

	//Audio which provides a reference on what (not) to cancel out
	std::unique_ptr<AudioBuffer> ref_audio = std::make_unique<AudioBuffer>(
		config.sample_rate_hz(), config.num_channels(),
		config.sample_rate_hz(), config.num_channels(),
		config.sample_rate_hz(), config.num_channels());
	//The audio upon which cancellation should be applied (processes in-place)
	std::unique_ptr<AudioBuffer> aec_audio = std::make_unique<AudioBuffer>(
		config.sample_rate_hz(), config.num_channels(),
		config.sample_rate_hz(), config.num_channels(),
		config.sample_rate_hz(), config.num_channels());
	constexpr int kLinearOutputRateHz = 16000;
	//Another output for the linear filter
	std::unique_ptr<AudioBuffer> aec_linear_audio = std::make_unique<AudioBuffer>(
		kLinearOutputRateHz, config.num_channels(),
		kLinearOutputRateHz, config.num_channels(),
		kLinearOutputRateHz, config.num_channels());

	AudioFrame ref_frame, aec_frame;

	int samples_per_frame = sample_rate / 100;
	int bytes_per_frame = samples_per_frame * bits_per_sample / 8;
	int total = rec_samples < ref_samples ? rec_samples / samples_per_frame : rec_samples / samples_per_frame;

	void* h_out = wav_write_open(argv[3], rec_sample_rate, rec_bits_per_sample, rec_channels, total * bytes_per_frame);
	void* h_linear_out = wav_write_open("linear.wav", kLinearOutputRateHz, rec_bits_per_sample, rec_channels, total * bytes_per_frame);

	int current = 0;
	unsigned char* ref_tmp = new unsigned char[bytes_per_frame];
	unsigned char* aec_tmp = new unsigned char[bytes_per_frame];
	cout << "processing audio frames ..." << endl;
	while (current++ < total) 
	{
		print_progress(current, total);
		//These methods appear to self-increment pointer offsets to each frame, via
		//wr->data_length -= length;
		wav_read_data(h_ref, ref_tmp, bytes_per_frame);
		wav_read_data(h_rec, aec_tmp, bytes_per_frame);

		ref_frame.UpdateFrame(0, reinterpret_cast<int16_t*>(ref_tmp), samples_per_frame, sample_rate, AudioFrame::kNormalSpeech, AudioFrame::kVadActive, 1);
		aec_frame.UpdateFrame(0, reinterpret_cast<int16_t*>(aec_tmp), samples_per_frame, sample_rate, AudioFrame::kNormalSpeech, AudioFrame::kVadActive, 1);

		ref_audio->CopyFrom(&ref_frame);
		aec_audio->CopyFrom(&aec_frame);
		//Time->Freq
		ref_audio->SplitIntoFrequencyBands();
		echo_controller->AnalyzeRender(ref_audio.get());
		//Freq->Time
		ref_audio->MergeFrequencyBands();
		echo_controller->AnalyzeCapture(aec_audio.get());
		//Time->Freq
		aec_audio->SplitIntoFrequencyBands();
		//Nyquist based on sample rate
		hp_filter->Process(aec_audio.get(), true);
		//Critical to apply non-0 value here if fixed minimum latency is known
		echo_controller->SetAudioBufferDelay(0);
		//Opt. for streaming: No need to render linear filter output
		echo_controller->ProcessCapture(aec_audio.get(), aec_linear_audio.get(), false);
		//Freq->Time
		aec_audio->MergeFrequencyBands();

		aec_audio->CopyTo(&aec_frame);
		memcpy(aec_tmp, aec_frame.data(), bytes_per_frame);
		wav_write_data(h_out, aec_tmp, bytes_per_frame);

		aec_frame.UpdateFrame(0, nullptr, kLinearOutputRateHz / 100, kLinearOutputRateHz, AudioFrame::kNormalSpeech, AudioFrame::kVadActive, 1);
		aec_linear_audio->CopyTo(&aec_frame);
		memcpy(aec_tmp, aec_frame.data(), 320);
		wav_write_data(h_linear_out, aec_tmp, 320);
	}

	delete[] ref_tmp;
	delete[] aec_tmp;

	if (CHECK_KPIS)
	{
		if (check_KPIs(3, &argv[1]) == true)
			cout << "check_KPIs: OK" << endl;
		else
			cout << "check_KPIs: Failed" << endl;

	}

	wav_read_close(h_ref);
	wav_read_close(h_rec);
	wav_write_close(h_out);
	wav_write_close(h_linear_out);

	return 0;
}