# Copyright 2022 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cmake_minimum_required(VERSION 3.13..3.22)
project(tesseract-sapi CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if(NOT TARGET sapi::sapi)
  set(SAPI_ROOT "../.." CACHE PATH "Path to the Sandboxed API source tree")
  add_subdirectory("${SAPI_ROOT}"
                   "${CMAKE_BINARY_DIR}/sandboxed-api-build"
                   EXCLUDE_FROM_ALL)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(tesseract IMPORTED_TARGET tesseract)
if(NOT tesseract_FOUND)
  return()
endif()

add_sapi_library(tesseract_sapi
  INPUTS "${tesseract_INCLUDEDIR}/tesseract/capi.h"
  LIBRARY tesseract
  FUNCTIONS
    TessAltoRendererCreate
    TessBaseAPIAdaptToWordStr
    TessBaseAPIAllWordConfidences
    TessBaseAPIAnalyseLayout
    TessBaseAPIClear
    TessBaseAPIClearAdaptiveClassifier
    TessBaseAPIClearPersistentCache
    TessBaseAPICreate
    TessBaseAPIDelete
    TessBaseAPIDetectOrientationScript
    TessBaseAPIEnd
    TessBaseAPIFindLinesCreateBlockList
    TessBaseAPIGetAltoText
    TessBaseAPIGetAvailableLanguagesAsVector
    TessBaseAPIGetBoolVariable
    TessBaseAPIGetBoxText
    TessBaseAPIGetComponentImages
    TessBaseAPIGetComponentImages1
    TessBaseAPIGetConnectedComponents
    TessBaseAPIGetDatapath
    TessBaseAPIGetDawg
    TessBaseAPIGetDoubleVariable
    TessBaseAPIGetFeaturesForBlob
    TessBaseAPIGetHOCRText
    TessBaseAPIGetInitLanguagesAsString
    TessBaseAPIGetInputImage
    TessBaseAPIGetInputName
    TessBaseAPIGetIntVariable
    TessBaseAPIGetIterator
    TessBaseAPIGetLSTMBoxText
    TessBaseAPIGetLoadedLanguagesAsVector
    TessBaseAPIGetMutableIterator
    TessBaseAPIGetOpenCLDevice
    TessBaseAPIGetPageSegMode
    TessBaseAPIGetRegions
    TessBaseAPIGetSourceYResolution
    TessBaseAPIGetStringVariable
    TessBaseAPIGetStrips
    TessBaseAPIGetTextDirection
    TessBaseAPIGetTextlines
    TessBaseAPIGetTextlines1
    TessBaseAPIGetThresholdedImage
    TessBaseAPIGetThresholdedImageScaleFactor
    TessBaseAPIGetTsvText
    TessBaseAPIGetUNLVText
    TessBaseAPIGetUTF8Text
    TessBaseAPIGetUnichar
    TessBaseAPIGetVariableAsString
    TessBaseAPIGetWordStrBoxText
    TessBaseAPIGetWords
    TessBaseAPIInit1
    TessBaseAPIInit2
    TessBaseAPIInit3
    TessBaseAPIInit4
    TessBaseAPIInitForAnalysePage
    TessBaseAPIInitLangMod
    TessBaseAPIInitTruthCallback
    TessBaseAPIIsValidWord
    TessBaseAPIMeanTextConf
    TessBaseAPINumDawgs
    TessBaseAPIOem
# these crash the code generator
    TessBaseAPIPrintVariables
    TessBaseAPIPrintVariablesToFile
    TessBaseAPIProcessPage
    TessBaseAPIProcessPages
    TessBaseAPIReadConfigFile
    TessBaseAPIReadDebugConfigFile
    TessBaseAPIRecognize
    TessBaseAPIRecognizeForChopTest
    TessBaseAPIRect
    TessBaseAPIRunAdaptiveClassifier
    TessBaseAPISetDebugVariable
    TessBaseAPISetDictFunc
    TessBaseAPISetImage
    TessBaseAPISetImage2
    TessBaseAPISetInputImage
    TessBaseAPISetInputName
    TessBaseAPISetMinOrientationMargin
    TessBaseAPISetOutputName
    TessBaseAPISetPageSegMode
    TessBaseAPISetProbabilityInContextFunc
    TessBaseAPISetRectangle
    TessBaseAPISetSourceResolution
    TessBaseAPISetThresholder
    TessBaseAPISetVariable
    TessBaseGetBlockTextOrientations
    TessBoxTextRendererCreate
    TessChoiceIteratorConfidence
    TessChoiceIteratorDelete
    TessChoiceIteratorGetUTF8Text
    TessChoiceIteratorNext
    TessDeleteBlockList
    TessDeleteIntArray
    TessDeleteResultRenderer
    TessDeleteText
    TessDeleteTextArray
    TessFindRowForBox
    TessHOcrRendererCreate
    TessHOcrRendererCreate2
    TessLSTMBoxRendererCreate
    TessMakeTBLOB
    TessMakeTessOCRRow
    TessMonitorCreate
    TessMonitorDelete
    TessMonitorGetCancelThis
    TessMonitorGetProgress
    TessMonitorSetCancelFunc
    TessMonitorSetCancelThis
    TessMonitorSetDeadlineMSecs
    TessMonitorSetProgressFunc
    TessNormalizeTBLOB
    TessPDFRendererCreate
    TessPageIteratorBaseline
    TessPageIteratorBegin
    TessPageIteratorBlockType
    TessPageIteratorBoundingBox
    TessPageIteratorCopy
    TessPageIteratorDelete
    TessPageIteratorGetBinaryImage
    TessPageIteratorGetImage
    TessPageIteratorIsAtBeginningOf
    TessPageIteratorIsAtFinalElement
    TessPageIteratorNext
    TessPageIteratorOrientation
    TessPageIteratorParagraphInfo
    TessResultIteratorConfidence
    TessResultIteratorCopy
    TessResultIteratorDelete
    TessResultIteratorGetChoiceIterator
    TessResultIteratorGetPageIterator
    TessResultIteratorGetPageIteratorConst
    TessResultIteratorGetUTF8Text
    TessResultIteratorNext
    TessResultIteratorSymbolIsDropcap
    TessResultIteratorSymbolIsSubscript
    TessResultIteratorSymbolIsSuperscript
    TessResultIteratorWordFontAttributes
    TessResultIteratorWordIsFromDictionary
    TessResultIteratorWordIsNumeric
    TessResultIteratorWordRecognitionLanguage
    TessResultRendererAddImage
    TessResultRendererBeginDocument
    TessResultRendererEndDocument
    TessResultRendererExtention
    TessResultRendererImageNum
    TessResultRendererInsert
    TessResultRendererNext
    TessResultRendererTitle
    TessTextRendererCreate
    TessTsvRendererCreate
    TessUnlvRendererCreate
    TessVersion
    TessWordStrBoxRendererCreate
  LIBRARY_NAME Tess
  NAMESPACE "tess_sapi"
)
add_library(sapi_contrib::tesseract ALIAS tesseract_sapi)
target_include_directories(tesseract_sapi INTERFACE
  "${PROJECT_BINARY_DIR}"
)
if(SAPI_ENABLE_TESTS)
  add_executable(tesseract_sapi_test
    sapi_tesseract_test.cc
  )
  target_link_libraries(tesseract_sapi_test PRIVATE
    sapi_contrib::tesseract
    sapi::test_main
  )
  gtest_discover_tests(tesseract_sapi_test)
endif()
