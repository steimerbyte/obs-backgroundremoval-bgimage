// SPDX-FileCopyrightText: 2021-2026 Roy Shilkrot <roy.shil@gmail.com>
// SPDX-FileCopyrightText: 2023-2026 Kaito Udagawa <umireon@kaito.tokyo>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <onnxruntime_cxx_api.h>
#include <cpu_provider_factory.h>
#include <filesystem>

#if defined(__APPLE__)
#include <coreml_provider_factory.h>
#endif

#ifdef _WIN32
#include <wchar.h>
#include <windows.h>
#endif // _WIN32

#include <obs-module.h>

#include "ort-session-utils.hpp"
#include "../consts.h"
#include "../plugin-support.h"

int createOrtSession(filter_data *tf)
{
	if (tf->model.get() == nullptr) {
		obs_log(LOG_ERROR, "Model object is not initialized");
		return OBS_BGREMOVAL_ORT_SESSION_ERROR_INVALID_MODEL;
	}

	Ort::SessionOptions sessionOptions;

	sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
	if (tf->useGPU != USEGPU_CPU) {
		sessionOptions.DisableMemPattern();
		sessionOptions.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
	} else {
		sessionOptions.SetInterOpNumThreads(tf->numThreads);
		sessionOptions.SetIntraOpNumThreads(tf->numThreads);
	}

	char *modelFilepath_rawPtr = obs_module_file(tf->modelSelection.c_str());

	if (modelFilepath_rawPtr == nullptr) {
		obs_log(LOG_ERROR, "Unable to get model filename %s from plugin.", tf->modelSelection.c_str());
		return OBS_BGREMOVAL_ORT_SESSION_ERROR_FILE_NOT_FOUND;
	}

	std::string modelFilepath_s(modelFilepath_rawPtr);

#if _WIN32
	int outLength = MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, modelFilepath_rawPtr, -1, nullptr, 0);
	tf->modelFilepath = std::wstring(outLength, L'\0');
	MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, modelFilepath_rawPtr, -1, tf->modelFilepath.data(), outLength);
#else
	tf->modelFilepath = std::string(modelFilepath_rawPtr);
#endif

	bfree(modelFilepath_rawPtr);

	try {
#ifdef HAVE_ONNXRUNTIME_CUDA_EP
		if (tf->useGPU == USEGPU_CUDA) {
			Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_CUDA(sessionOptions, 0));
		}
#endif
#ifdef HAVE_ONNXRUNTIME_ROCM_EP
		if (tf->useGPU == USEGPU_ROCM) {
			Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_ROCM(sessionOptions, 0));
		}
#endif
#ifdef HAVE_ONNXRUNTIME_MIGRAPHX_EP
		if (tf->useGPU == USEGPU_MIGRAPHX) {
			Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_MIGraphX(sessionOptions, 0));
		}
#endif
#ifdef HAVE_ONNXRUNTIME_TENSORRT_EP
		if (tf->useGPU == USEGPU_TENSORRT) {
			Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_Tensorrt(sessionOptions, 0));
		}
#endif
#if defined(__APPLE__)
		if (tf->useGPU == USEGPU_COREML) {
			uint32_t coreml_flags = 0;
			coreml_flags |= COREML_FLAG_ENABLE_ON_SUBGRAPH;
			Ort::ThrowOnError(
				OrtSessionOptionsAppendExecutionProvider_CoreML(sessionOptions, coreml_flags));
		}
#endif
		tf->session.reset(new Ort::Session(*tf->env, tf->modelFilepath.c_str(), sessionOptions));
	} catch (const std::exception &e) {
		obs_log(LOG_ERROR, "%s", e.what());
		return OBS_BGREMOVAL_ORT_SESSION_ERROR_STARTUP;
	}

	Ort::AllocatorWithDefaultOptions allocator;

	tf->model->populateInputOutputNames(tf->session, tf->inputNames, tf->outputNames);

	// Cache the char* names for Ort::Run once per session instead of per frame.
	tf->rawInputNames.clear();
	for (auto &name : tf->inputNames)
		tf->rawInputNames.push_back(name.get());
	tf->rawOutputNames.clear();
	for (auto &name : tf->outputNames)
		tf->rawOutputNames.push_back(name.get());

	if (!tf->model->populateInputOutputShapes(tf->session, tf->inputDims, tf->outputDims)) {
		obs_log(LOG_ERROR, "Unable to get model input and output shapes");
		return OBS_BGREMOVAL_ORT_SESSION_ERROR_INVALID_INPUT_OUTPUT;
	}

	for (size_t i = 0; i < tf->inputNames.size(); i++) {
		obs_log(LOG_INFO, "Model %s input %d: name %s shape (%d dim) %d x %d x %d x %d",
			tf->modelSelection.c_str(), (int)i, tf->inputNames[i].get(), (int)tf->inputDims[i].size(),
			(int)tf->inputDims[i][0], ((int)tf->inputDims[i].size() > 1) ? (int)tf->inputDims[i][1] : 0,
			((int)tf->inputDims[i].size() > 2) ? (int)tf->inputDims[i][2] : 0,
			((int)tf->inputDims[i].size() > 3) ? (int)tf->inputDims[i][3] : 0);
	}
	for (size_t i = 0; i < tf->outputNames.size(); i++) {
		obs_log(LOG_INFO, "Model %s output %d: name %s shape (%d dim) %d x %d x %d x %d",
			tf->modelSelection.c_str(), (int)i, tf->outputNames[i].get(), (int)tf->outputDims[i].size(),
			(int)tf->outputDims[i][0], ((int)tf->outputDims[i].size() > 1) ? (int)tf->outputDims[i][1] : 0,
			((int)tf->outputDims[i].size() > 2) ? (int)tf->outputDims[i][2] : 0,
			((int)tf->outputDims[i].size() > 3) ? (int)tf->outputDims[i][3] : 0);
	}

	// Allocate buffers
	tf->model->allocateTensorBuffers(tf->inputDims, tf->outputDims, tf->outputTensorValues, tf->inputTensorValues,
					 tf->inputTensor, tf->outputTensor);

	return OBS_BGREMOVAL_ORT_SESSION_SUCCESS;
}

bool runFilterModelInference(filter_data *tf, const cv::Mat &imageBGRA, cv::Mat &output)
{
	if (tf->session.get() == nullptr) {
		// Onnx runtime session is not initialized. Problem in initialization
		return false;
	}
	if (tf->model.get() == nullptr) {
		// Model object is not initialized
		return false;
	}

	// Resize to network input size
	uint32_t inputWidth, inputHeight;
	tf->model->getNetworkInputSize(tf->inputDims, inputWidth, inputHeight);

	// Resize before the BGRA->RGB conversion: resize works per channel, so the result is
	// bit-identical, but the full-resolution channel permutation and its buffer are gone.
	cv::resize(imageBGRA, tf->inferenceBGRA, cv::Size(inputWidth, inputHeight), 0, 0, cv::INTER_LINEAR);
	cv::cvtColor(tf->inferenceBGRA, tf->inferenceRGB, cv::COLOR_BGRA2RGB);

	// Prepare input to network. These Mats persist, so nothing is allocated per frame.
	tf->inferenceRGB.convertTo(tf->inferenceF32, CV_32F);

	tf->model->prepareInputToNetwork(tf->inferenceF32, tf->preprocessed);

	tf->model->loadInputToTensor(tf->preprocessed, inputWidth, inputHeight, tf->inputTensorValues);

	// Run network inference
	tf->model->runNetworkInference(tf->session, tf->rawInputNames, tf->rawOutputNames, tf->inputTensor,
				       tf->outputTensor);

	// Get output
	// Map network output to cv::Mat
	cv::Mat outputImage = tf->model->getNetworkOutput(tf->outputDims, tf->outputTensorValues);

	// Assign output to input in some models that have temporal information
	tf->model->assignOutputToInput(tf->outputTensorValues, tf->inputTensorValues);

	// Post-process output. The image will now be in [0,1] float, BHWC format
	tf->model->postprocessOutput(outputImage);

	// Convert [0,1] float to CV_8U [0,255]
	outputImage.convertTo(output, CV_8U, 255.0);

	return true;
}
