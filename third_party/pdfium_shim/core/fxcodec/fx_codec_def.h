#pragma once
enum class FXCODEC_STATUS {
    kError = -1, kFrameReady = 0, kFrameToBeContinued,
    kDecodeReady, kDecodeToBeContinued, kDecodeFinished,
};