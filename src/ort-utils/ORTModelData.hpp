// SPDX-FileCopyrightText: 2021-2026 Roy Shilkrot <roy.shil@gmail.com>
// SPDX-FileCopyrightText: 2023-2026 Kaito Udagawa <umireon@kaito.tokyo>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ORTMODELDATA_H
#define ORTMODELDATA_H

#include <onnxruntime_cxx_api.h>

#include <opencv2/core.hpp>

struct ORTModelData {
	std::unique_ptr<Ort::Session> session;
	std::unique_ptr<Ort::Env> env;
	std::vector<Ort::AllocatedStringPtr> inputNames;
	std::vector<Ort::AllocatedStringPtr> outputNames;
	std::vector<const char *> rawInputNames;
	std::vector<const char *> rawOutputNames;
	std::vector<Ort::Value> inputTensor;
	std::vector<Ort::Value> outputTensor;
	std::vector<std::vector<int64_t>> inputDims;
	std::vector<std::vector<int64_t>> outputDims;
	std::vector<std::vector<float>> outputTensorValues;
	std::vector<std::vector<float>> inputTensorValues;

	// Reused inference buffers (sized from the model input dims on the first frame) so that
	// preprocessing does not allocate per frame.
	cv::Mat inferenceBGRA, inferenceRGB, inferenceF32, preprocessed;
};

#endif /* ORTMODELDATA_H */
