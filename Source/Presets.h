#pragma once

#include "Parameters.h"
#include <vector>

namespace kd
{
    /** A factory preset: every parameter at its default, except the listed ones. Samples are kept. */
    struct Preset
    {
        const char* name;
        std::vector<std::pair<juce::String, float>> values;
    };

    inline const std::vector<Preset>& getFactoryPresets()
    {
        using namespace ParamIDs;
        auto b = [] (int band, const char* p) { return bandParam (band, p); };

        static const std::vector<Preset> presets
        {
            { "Init", {} },

            { "Tight Club",
              { { b (0, BandParams::length), 340.0f }, { b (0, BandParams::decay), 220.0f },
                { b (1, BandParams::length), 160.0f },  { b (1, BandParams::decay), 120.0f },
                { b (2, BandParams::length), 68.0f },   { b (2, BandParams::decay), 60.0f },
                { b (3, BandParams::length), 32.0f },   { b (3, BandParams::decay), 30.0f },
                { compOn, 1.0f }, { compThresh, -14.0f }, { compRatio, 3.0f }, { compAttack, 15.0f }, { compRelease, 90.0f } } },

            { "Long 808 Tail",
              { { b (0, BandParams::decay), 1800.0f }, { b (0, BandParams::decayMode), 1.0f }, { b (0, BandParams::sync), 4.0f },
                { b (0, BandParams::curve), 35.0f },
                { b (1, BandParams::length), 310.0f },  { b (1, BandParams::decay), 250.0f },
                { b (2, BandParams::length), 90.0f },  { b (2, BandParams::decay), 80.0f },
                { b (3, BandParams::length), 44.0f },   { b (3, BandParams::decay), 40.0f },
                { tailOn, 1.0f }, { tailStart, 140.0f }, { tailGlide, 120.0f },
                { satOn, 1.0f }, { satType, 1.0f }, { satDrive, 4.0f } } },

            { "Half Note Boom",
              { { b (0, BandParams::decayMode), 1.0f }, { b (0, BandParams::sync), 3.0f }, { b (0, BandParams::decay), 850.0f },
                { b (1, BandParams::decayMode), 1.0f }, { b (1, BandParams::sync), 1.0f },
                { b (2, BandParams::decay), 90.0f }, { b (2, BandParams::length), 100.0f },
                { b (3, BandParams::decay), 40.0f }, { b (3, BandParams::length), 44.0f },
                { tailOn, 1.0f }, { tuneBody, 1.0f } } },

            { "Punchy Short",
              { { b (0, BandParams::length), 220.0f },  { b (0, BandParams::decay), 160.0f }, { b (0, BandParams::curve), 70.0f },
                { b (1, BandParams::length), 115.0f },  { b (1, BandParams::decay), 90.0f },  { b (1, BandParams::level), 2.0f },
                { b (2, BandParams::length), 45.0f },   { b (2, BandParams::decay), 40.0f },  { b (2, BandParams::level), 3.0f },
                { b (3, BandParams::length), 21.0f },   { b (3, BandParams::decay), 20.0f },
                { compOn, 1.0f }, { compThresh, -18.0f }, { compRatio, 6.0f }, { compAttack, 25.0f }, { compRelease, 60.0f },
                { compMakeup, 4.0f } } },

            { "Saturated Techno",
              { { b (0, BandParams::decayMode), 1.0f }, { b (0, BandParams::sync), 2.0f },
                { b (1, BandParams::decayMode), 1.0f }, { b (1, BandParams::sync), 1.0f },
                { b (2, BandParams::length), 80.0f }, { b (2, BandParams::decay), 70.0f },
                { b (3, BandParams::length), 32.0f }, { b (3, BandParams::decay), 30.0f },
                { satOn, 1.0f }, { satType, 2.0f }, { satDrive, 14.0f }, { satTone, 70.0f }, { satLevel, -4.0f },
                { compOn, 1.0f }, { compThresh, -10.0f }, { compRatio, 4.0f } } },

            { "Clicky Top",
              { { b (2, BandParams::level), 6.0f }, { b (2, BandParams::length), 50.0f }, { b (2, BandParams::decay), 45.0f },
                { b (3, BandParams::level), 4.0f }, { b (3, BandParams::length), 27.0f }, { b (3, BandParams::decay), 25.0f },
                { b (0, BandParams::length), 400.0f }, { b (0, BandParams::decay), 300.0f } } },

            { "Sub Only",
              { { b (1, BandParams::mute), 1.0f }, { b (2, BandParams::mute), 1.0f }, { b (3, BandParams::mute), 1.0f },
                { b (0, BandParams::decayMode), 1.0f }, { b (0, BandParams::sync), 2.0f }, { tailOn, 1.0f } } },

            { "Fuzz Distorted",
              { { satOn, 1.0f }, { satType, 5.0f }, { satDrive, 20.0f }, { satTone, 55.0f }, { satLevel, -8.0f },
                { b (0, BandParams::decayMode), 1.0f }, { b (0, BandParams::sync), 2.0f },
                { compOn, 1.0f }, { compThresh, -8.0f }, { compRatio, 8.0f }, { compAttack, 2.0f } } },
        };
        return presets;
    }
}
