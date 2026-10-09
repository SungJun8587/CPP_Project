
//***************************************************************************
// ChatStressReporter.h : interface for the console report functions.
//
//***************************************************************************

#ifndef UC_CHATSTRESSREPORTER_H
#define UC_CHATSTRESSREPORTER_H

#include "ChatStressStats.h"
#include "ChatStressConfig.h"

#include <cstdio>
#include <cstdint>

void ReportSecond(double elapsedSec, double dtSec);
void PrintSummary(double elapsedSec);

#endif // ndef UC_CHATSTRESSREPORTER_H