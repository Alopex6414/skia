/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "CodecPriv.h"
#include "Resources.h"
#include "SkAndroidCodec.h"
#include "SkAnimatedImage.h"
#include "SkBitmap.h"
#include "SkCanvas.h"
#include "SkCodec.h"
#include "SkColor.h"
#include "SkData.h"
#include "SkImageInfo.h"
#include "SkPicture.h"
#include "SkRefCnt.h"
#include "SkSize.h"
#include "SkString.h"
#include "SkTypes.h"
#include "SkUnPreMultiply.h"
#include "Test.h"
#include "sk_tool_utils.h"

#include <algorithm>
#include <memory>
#include <vector>

DEF_TEST(AnimatedImage_scaled, r) {
    if (GetResourcePath().isEmpty()) {
        return;
    }

    const char* file = "images/alphabetAnim.gif";
    auto data = GetResourceAsData(file);
    if (!data) {
        ERRORF(r, "Could not get %s", file);
        return;
    }

    auto codec = SkAndroidCodec::MakeFromCodec(SkCodec::MakeFromData(data));
    if (!codec) {
        ERRORF(r, "Could not create codec for %s", file);
        return;
    }

    // Force the drawable follow its special case that requires scaling.
    auto size = codec->getInfo().dimensions();
    size.set(size.width() - 5, size.height() - 5);
    auto rect = SkIRect::MakeSize(size);
    auto image = SkAnimatedImage::Make(std::move(codec), size, rect, nullptr);
    if (!image) {
        ERRORF(r, "Failed to create animated image for %s", file);
        return;
    }

    // Clear a bitmap to non-transparent and draw to it. pixels that are transparent
    // in the image should not replace the original non-transparent color.
    SkBitmap bm;
    bm.allocPixels(SkImageInfo::MakeN32Premul(size.width(), size.height()));
    bm.eraseColor(SK_ColorBLUE);
    SkCanvas canvas(bm);
    image->draw(&canvas);
    for (int i = 0; i < size.width();  ++i)
    for (int j = 0; j < size.height(); ++j) {
        if (*bm.getAddr32(i, j) == SK_ColorTRANSPARENT) {
            ERRORF(r, "Erased color underneath!");
            return;
        }
    }
}

DEF_TEST(AnimatedImage, r) {
    if (GetResourcePath().isEmpty()) {
        return;
    }
    for (const char* file : { "images/alphabetAnim.gif",
                              "images/colorTables.gif",
                              "images/webp-animated.webp",
                              "images/required.webp",
                              }) {
        auto data = GetResourceAsData(file);
        if (!data) {
            ERRORF(r, "Could not get %s", file);
            continue;
        }

        auto codec = SkCodec::MakeFromData(data);
        if (!codec) {
            ERRORF(r, "Could not create codec for %s", file);
            continue;
        }

        const int defaultRepetitionCount = codec->getRepetitionCount();
        std::vector<SkCodec::FrameInfo> frameInfos = codec->getFrameInfo();
        std::vector<SkBitmap> frames(frameInfos.size());
        // Used down below for our test image.
        const auto imageInfo = codec->getInfo().makeAlphaType(kPremul_SkAlphaType);

        for (size_t i = 0; i < frameInfos.size(); ++i) {
            auto info = codec->getInfo().makeAlphaType(frameInfos[i].fAlphaType);
            auto& bm = frames[i];

            SkCodec::Options options;
            options.fFrameIndex = (int) i;
            options.fPriorFrame = frameInfos[i].fRequiredFrame;
            if (options.fPriorFrame == SkCodec::kNone) {
                bm.allocPixels(info);
                bm.eraseColor(0);
            } else {
                const SkBitmap& priorFrame = frames[options.fPriorFrame];
                if (!sk_tool_utils::copy_to(&bm, priorFrame.colorType(), priorFrame)) {
                    ERRORF(r, "Failed to copy %s frame %i", file, options.fPriorFrame);
                    options.fPriorFrame = SkCodec::kNone;
                }
                REPORTER_ASSERT(r, bm.setAlphaType(frameInfos[i].fAlphaType));
            }

            auto result = codec->getPixels(info, bm.getPixels(), bm.rowBytes(), &options);
            if (result != SkCodec::kSuccess) {
                ERRORF(r, "error in %s frame %zu: %s", file, i, SkCodec::ResultToString(result));
            }
        }

        auto androidCodec = SkAndroidCodec::MakeFromCodec(std::move(codec));
        if (!androidCodec) {
            ERRORF(r, "Could not create androidCodec for %s", file);
            continue;
        }

        auto animatedImage = SkAnimatedImage::Make(std::move(androidCodec));
        if (!animatedImage) {
            ERRORF(r, "Could not create animated image for %s", file);
            continue;
        }

        REPORTER_ASSERT(r, defaultRepetitionCount == animatedImage->getRepetitionCount());

        auto testDraw = [r, &frames, &imageInfo, file](const sk_sp<SkAnimatedImage>& animatedImage,
                                                       int expectedFrame) {
            SkBitmap test;
            test.allocPixels(imageInfo);
            test.eraseColor(0);
            SkCanvas c(test);
            animatedImage->draw(&c);

            const SkBitmap& frame = frames[expectedFrame];
            REPORTER_ASSERT(r, frame.colorType() == test.colorType());
            REPORTER_ASSERT(r, frame.dimensions() == test.dimensions());
            for (int i = 0; i < test.width();  ++i)
            for (int j = 0; j < test.height(); ++j) {
                SkColor expected = SkUnPreMultiply::PMColorToColor(*frame.getAddr32(i, j));
                SkColor actual   = SkUnPreMultiply::PMColorToColor(*test .getAddr32(i, j));
                if (expected != actual) {
                    ERRORF(r, "frame %i of %s does not match at pixel %i, %i!"
                            " expected %x\tactual: %x",
                            expectedFrame, file, i, j, expected, actual);
                    SkString expected_name = SkStringPrintf("expected_%c", '0' + expectedFrame);
                    SkString actual_name   = SkStringPrintf("actual_%c",   '0' + expectedFrame);
                    write_bm(expected_name.c_str(), frame);
                    write_bm(actual_name.c_str(),   test);
                    return false;
                }
            }
            return true;
        };

        REPORTER_ASSERT(r, animatedImage->currentFrameDuration() == frameInfos[0].fDuration);

        if (!testDraw(animatedImage, 0)) {
            ERRORF(r, "Did not start with frame 0");
            continue;
        }

        // Start at an arbitrary time.
        bool failed = false;
        for (size_t i = 1; i < frameInfos.size(); ++i) {
            const int frameTime = animatedImage->decodeNextFrame();
            REPORTER_ASSERT(r, frameTime == animatedImage->currentFrameDuration());

            if (i == frameInfos.size() - 1 && defaultRepetitionCount == 0) {
                REPORTER_ASSERT(r, frameTime == SkAnimatedImage::kFinished);
                REPORTER_ASSERT(r, animatedImage->isFinished());
            } else {
                REPORTER_ASSERT(r, frameTime == frameInfos[i].fDuration);
                REPORTER_ASSERT(r, !animatedImage->isFinished());
            }

            if (!testDraw(animatedImage, i)) {
                ERRORF(r, "Did not update to %i properly", i);
                failed = true;
                break;
            }
        }

        if (failed) {
            continue;
        }

        animatedImage->reset();
        REPORTER_ASSERT(r, !animatedImage->isFinished());
        if (!testDraw(animatedImage, 0)) {
            ERRORF(r, "reset failed");
            continue;
        }

        // Test reset from all the frames.
        // j is the frame to call reset on.
        for (int j = 0; j < (int) frameInfos.size(); ++j) {
            if (failed) {
                break;
            }

            // i is the frame to decode.
            for (int i = 0; i <= j; ++i) {
                if (i == j) {
                    animatedImage->reset();
                    if (!testDraw(animatedImage, 0)) {
                        ERRORF(r, "reset failed for image %s from frame %i",
                                file, i);
                        failed = true;
                        break;
                    }
                } else if (i != 0) {
                    animatedImage->decodeNextFrame();
                    if (!testDraw(animatedImage, i)) {
                        ERRORF(r, "failed to match frame %i in %s on iteration %i",
                                i, file, j);
                        failed = true;
                        break;
                    }
                }
            }
        }

        if (failed) {
            continue;
        }

        for (int loopCount : { 0, 1, 2, 5 }) {
            animatedImage = SkAnimatedImage::Make(SkAndroidCodec::MakeFromCodec(
                        SkCodec::MakeFromData(data)));
            animatedImage->setRepetitionCount(loopCount);
            REPORTER_ASSERT(r, animatedImage->getRepetitionCount() == loopCount);

            for (int loops = 0; loops <= loopCount; loops++) {
                if (failed) {
                    break;
                }
                REPORTER_ASSERT(r, !animatedImage->isFinished());
                for (size_t i = 1; i <= frameInfos.size(); ++i) {
                    const int frameTime = animatedImage->decodeNextFrame();
                    if (frameTime == SkAnimatedImage::kFinished) {
                        if (loops != loopCount) {
                            ERRORF(r, "%s animation stopped early: loops: %i\tloopCount: %i",
                                    file, loops, loopCount);
                            failed = true;
                        }
                        if (i != frameInfos.size() - 1) {
                            ERRORF(r, "%s animation stopped early: i: %i\tsize: %i",
                                    file, i, frameInfos.size());
                            failed = true;
                        }
                        break;
                    }
                }
            }

            if (!animatedImage->isFinished()) {
                ERRORF(r, "%s animation should have finished with specified loop count (%i)",
                          file, loopCount);
            }
        }
    }
}
