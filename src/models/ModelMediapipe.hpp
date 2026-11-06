// SPDX-FileCopyrightText: 2021-2026 Roy Shilkrot <roy.shil@gmail.com>
// SPDX-FileCopyrightText: 2023-2026 Kaito Udagawa <umireon@kaito.tokyo>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MODELMEDIAPIPE_H
#define MODELMEDIAPIPE_H

#include "Model.hpp"

class ModelMediaPipe : public Model {
private:
	cv::Mat outputChannel1;

public:
	ModelMediaPipe(/* args */) {}
	~ModelMediaPipe() {}

	virtual cv::Mat getNetworkOutput(const std::vector<std::vector<int64_t>> &outputDims,
					 std::vector<std::vector<float>> &outputTensorValues)
	{
		uint32_t outputWidth = (int)outputDims[0].at(2);
		uint32_t outputHeight = (int)outputDims[0].at(1);
		int32_t outputChannels = CV_32FC2;

		return cv::Mat(outputHeight, outputWidth, outputChannels, outputTensorValues[0].data());
	}

	virtual void postprocessOutput(cv::Mat &outputImage)
	{
		// take 2nd channel, into a reused buffer (cv::split would allocate two Mats)
		cv::extractChannel(outputImage, outputChannel1, 1);

		outputImage = outputChannel1;
	}
};

#endif // MODELMEDIAPIPE_H
