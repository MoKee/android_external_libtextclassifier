/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "annotator/duration/duration.h"

#include <string>
#include <vector>

#include "annotator/collections.h"
#include "annotator/model_generated.h"
#include "annotator/types-test-util.h"
#include "annotator/types.h"
#include "utils/test-utils.h"
#include "utils/utf8/unicodetext.h"
#include "utils/utf8/unilib.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace libtextclassifier3 {
namespace {

const DurationAnnotatorOptions* TestingDurationAnnotatorOptions() {
  static const flatbuffers::DetachedBuffer* options_data = []() {
    DurationAnnotatorOptionsT options;
    options.enabled = true;

    options.week_expressions.push_back("week");
    options.week_expressions.push_back("weeks");

    options.day_expressions.push_back("day");
    options.day_expressions.push_back("days");

    options.hour_expressions.push_back("hour");
    options.hour_expressions.push_back("hours");

    options.minute_expressions.push_back("minute");
    options.minute_expressions.push_back("minutes");

    options.second_expressions.push_back("second");
    options.second_expressions.push_back("seconds");

    options.filler_expressions.push_back("and");
    options.filler_expressions.push_back("a");
    options.filler_expressions.push_back("an");
    options.filler_expressions.push_back("one");

    options.half_expressions.push_back("half");

    flatbuffers::FlatBufferBuilder builder;
    builder.Finish(DurationAnnotatorOptions::Pack(builder, &options));
    return new flatbuffers::DetachedBuffer(builder.Release());
  }();

  return flatbuffers::GetRoot<DurationAnnotatorOptions>(options_data->data());
}

FeatureProcessor BuildFeatureProcessor(const UniLib* unilib) {
  static const flatbuffers::DetachedBuffer* options_data = []() {
    FeatureProcessorOptionsT options;
    options.context_size = 1;
    options.max_selection_span = 1;
    options.snap_label_span_boundaries_to_containing_tokens = false;

    options.tokenization_codepoint_config.emplace_back(
        new TokenizationCodepointRangeT());
    auto& config = options.tokenization_codepoint_config.back();
    config->start = 32;
    config->end = 33;
    config->role = TokenizationCodepointRange_::Role_WHITESPACE_SEPARATOR;

    flatbuffers::FlatBufferBuilder builder;
    builder.Finish(FeatureProcessorOptions::Pack(builder, &options));
    return new flatbuffers::DetachedBuffer(builder.Release());
  }();

  const FeatureProcessorOptions* feature_processor_options =
      flatbuffers::GetRoot<FeatureProcessorOptions>(options_data->data());

  return FeatureProcessor(feature_processor_options, unilib);
}

class DurationAnnotatorTest : public ::testing::Test {
 protected:
  DurationAnnotatorTest()
      : INIT_UNILIB_FOR_TESTING(unilib_),
        feature_processor_(BuildFeatureProcessor(&unilib_)),
        duration_annotator_(TestingDurationAnnotatorOptions(),
                            &feature_processor_) {}

  std::vector<Token> Tokenize(const std::string& text) {
    return feature_processor_.Tokenize(text);
  }

  UniLib unilib_;
  FeatureProcessor feature_processor_;
  DurationAnnotator duration_annotator_;
};

TEST_F(DurationAnnotatorTest, ClassifiesSimpleDuration) {
  ClassificationResult classification;
  EXPECT_TRUE(duration_annotator_.ClassifyText(
      UTF8ToUnicodeText("Wake me up in 15 minutes ok?"), {14, 24},
      AnnotationUsecase_ANNOTATION_USECASE_RAW, &classification));

  EXPECT_EQ(classification.collection, "duration");
  EXPECT_EQ(classification.duration_ms, 15 * 60 * 1000);
}

TEST_F(DurationAnnotatorTest, ClassifiesWhenTokensDontAlignWithSelection) {
  ClassificationResult classification;
  EXPECT_TRUE(duration_annotator_.ClassifyText(
      UTF8ToUnicodeText("Wake me up in15 minutesok?"), {13, 23},
      AnnotationUsecase_ANNOTATION_USECASE_RAW, &classification));

  EXPECT_EQ(classification.collection, "duration");
  EXPECT_EQ(classification.duration_ms, 15 * 60 * 1000);
}

TEST_F(DurationAnnotatorTest, FindsSimpleDuration) {
  std::vector<AnnotatedSpan> result;
  EXPECT_TRUE(duration_annotator_.FindAll(
      Tokenize("Wake me up in 15 minutes ok?"),
      AnnotationUsecase_ANNOTATION_USECASE_RAW, &result));

  ASSERT_EQ(result.size(), 1);
  EXPECT_EQ(result[0].span, CodepointSpan(14, 24));
  ASSERT_EQ(result[0].classification.size(), 1);
  EXPECT_EQ(result[0].classification[0].collection, "duration");
  EXPECT_EQ(result[0].classification[0].duration_ms, 15 * 60 * 1000);
}

TEST_F(DurationAnnotatorTest, FindsDurationWithHalfExpression) {
  std::vector<AnnotatedSpan> result;
  EXPECT_TRUE(duration_annotator_.FindAll(
      Tokenize("Set a timer for 3 and half minutes ok?"),
      AnnotationUsecase_ANNOTATION_USECASE_RAW, &result));

  ASSERT_EQ(result.size(), 1);
  EXPECT_EQ(result[0].span, CodepointSpan(16, 34));
  ASSERT_EQ(result[0].classification.size(), 1);
  EXPECT_EQ(result[0].classification[0].collection, "duration");
  EXPECT_EQ(result[0].classification[0].duration_ms, 3.5 * 60 * 1000);
}

TEST_F(DurationAnnotatorTest, FindsComposedDuration) {
  std::vector<AnnotatedSpan> result;
  EXPECT_TRUE(duration_annotator_.FindAll(
      Tokenize("Wake me up in 3 hours and 5 seconds ok?"),
      AnnotationUsecase_ANNOTATION_USECASE_RAW, &result));

  ASSERT_EQ(result.size(), 1);
  EXPECT_EQ(result[0].span, CodepointSpan(14, 35));
  ASSERT_EQ(result[0].classification.size(), 1);
  EXPECT_EQ(result[0].classification[0].collection, "duration");
  EXPECT_EQ(result[0].classification[0].duration_ms,
            3 * 60 * 60 * 1000 + 5 * 1000);
}

TEST_F(DurationAnnotatorTest, FindsHalfAnHour) {
  std::vector<AnnotatedSpan> result;
  EXPECT_TRUE(duration_annotator_.FindAll(
      Tokenize("Set a timer for half an hour"),
      AnnotationUsecase_ANNOTATION_USECASE_RAW, &result));

  ASSERT_EQ(result.size(), 1);
  EXPECT_EQ(result[0].span, CodepointSpan(16, 28));
  ASSERT_EQ(result[0].classification.size(), 1);
  EXPECT_EQ(result[0].classification[0].collection, "duration");
  EXPECT_EQ(result[0].classification[0].duration_ms, 0.5 * 60 * 60 * 1000);
}

TEST_F(DurationAnnotatorTest, FindsWhenHalfIsAfterGranularitySpecification) {
  std::vector<AnnotatedSpan> result;
  EXPECT_TRUE(duration_annotator_.FindAll(
      Tokenize("Set a timer for 1 hour and a half"),
      AnnotationUsecase_ANNOTATION_USECASE_RAW, &result));

  ASSERT_EQ(result.size(), 1);
  EXPECT_EQ(result[0].span, CodepointSpan(16, 33));
  ASSERT_EQ(result[0].classification.size(), 1);
  EXPECT_EQ(result[0].classification[0].collection, "duration");
  EXPECT_EQ(result[0].classification[0].duration_ms, 1.5 * 60 * 60 * 1000);
}

TEST_F(DurationAnnotatorTest, FindsAnHourAndAHalf) {
  std::vector<AnnotatedSpan> result;
  EXPECT_TRUE(duration_annotator_.FindAll(
      Tokenize("Set a timer for an hour and a half"),
      AnnotationUsecase_ANNOTATION_USECASE_RAW, &result));

  ASSERT_EQ(result.size(), 1);
  EXPECT_EQ(result[0].span, CodepointSpan(19, 34));
  ASSERT_EQ(result[0].classification.size(), 1);
  EXPECT_EQ(result[0].classification[0].collection, "duration");
  EXPECT_EQ(result[0].classification[0].duration_ms, 1.5 * 60 * 60 * 1000);
}

TEST_F(DurationAnnotatorTest,
       FindsCorrectlyWhenSecondsComeSecondAndDontHaveNumber) {
  std::vector<AnnotatedSpan> result;
  EXPECT_TRUE(duration_annotator_.FindAll(
      Tokenize("Set a timer for 10 minutes and a second ok?"),
      AnnotationUsecase_ANNOTATION_USECASE_RAW, &result));

  ASSERT_EQ(result.size(), 1);
  EXPECT_EQ(result[0].span, CodepointSpan(16, 39));
  ASSERT_EQ(result[0].classification.size(), 1);
  EXPECT_EQ(result[0].classification[0].collection, "duration");
  EXPECT_EQ(result[0].classification[0].duration_ms, 10 * 60 * 1000 + 1 * 1000);
}

TEST_F(DurationAnnotatorTest, DoesNotGreedilyTakeFillerWords) {
  std::vector<AnnotatedSpan> result;
  EXPECT_TRUE(duration_annotator_.FindAll(
      Tokenize("Set a timer for a a a 10 minutes and 2 seconds an and an ok?"),
      AnnotationUsecase_ANNOTATION_USECASE_RAW, &result));

  ASSERT_EQ(result.size(), 1);
  EXPECT_EQ(result[0].span, CodepointSpan(22, 46));
  ASSERT_EQ(result[0].classification.size(), 1);
  EXPECT_EQ(result[0].classification[0].collection, "duration");
  EXPECT_EQ(result[0].classification[0].duration_ms, 10 * 60 * 1000 + 2 * 1000);
}

TEST_F(DurationAnnotatorTest, DoesNotCrashWhenJustHalfIsSaid) {
  std::vector<AnnotatedSpan> result;
  EXPECT_TRUE(duration_annotator_.FindAll(
      Tokenize("Set a timer for half ok?"),
      AnnotationUsecase_ANNOTATION_USECASE_RAW, &result));

  ASSERT_EQ(result.size(), 0);
}

}  // namespace
}  // namespace libtextclassifier3
