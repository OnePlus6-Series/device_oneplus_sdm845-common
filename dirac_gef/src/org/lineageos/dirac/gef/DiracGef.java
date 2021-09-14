/*
 * Copyright (c) 2021 The LineageOS Project
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

package org.lineageos.dirac.gef;

import android.media.audiofx.AudioEffect;

import java.util.UUID;

public class DiracGef extends AudioEffect {
    private static final String TAG = "DiracGef";

    private static final UUID EFFECT_TYPE_DIRAC_GEF =
            UUID.fromString("3799d6d1-22c5-43c3-b3ec-d664cf8d2f0d");

    public DiracGef(int priority, int audioSession) {
        super(EFFECT_TYPE_NULL, EFFECT_TYPE_DIRAC_GEF, priority, audioSession);
    }
}
