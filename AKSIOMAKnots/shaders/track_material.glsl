    else if (uMaterial == 7 && vTexcoord.w < 0.6)
    {
        /*
         * The railway (Photo::buildTrainTrack): vTexcoord holds the place along
         * it and across it (mm), and which rail length or sleeper this is;
         * its last number says what the surface is.
         */
        float along = vTexcoord.x;
        float across = vTexcoord.y;
        float part = vTexcoord.w;

        float footprint = max(fwidth(across), fwidth(along) * 0.3);
        float fine = 1.0 - smoothstep(0.45, 1.6, footprint);

        s.coat = 0.0;

        if (part < 0.15)
        {
            /*
             * Ballast: broken stone, a couple of millimetres across at this
             * scale, grey with a little brown, dark between the stones, dusty
             * and rust-stained near the rails. Far off, it is its mean colour.
             */
            vec2 p = vec2(along, across) / 1.35;
            vec2 cell = floor(p);

            float nearest = 8.0;
            float second = 8.0;
            float id = 0.0;

            for (int y = -1; y <= 1; ++y)
            {
                for (int x = -1; x <= 1; ++x)
                {
                    vec2 c = cell + vec2(float(x), float(y));

                    float seed = hash12(c);

                    vec2 o = c + 0.5 + 0.8 * (vec2(seed, hash12(c + 7.3)) - 0.5);

                    float d = length(p - o);

                    if (d < nearest)
                    {
                        second = nearest;
                        nearest = d;
                        id = seed;
                    }
                    else if (d < second)
                    {
                        second = d;
                    }
                }
            }

            float gap = smoothstep(0.0, 0.22, second - nearest);

            vec3 stone = mix(vec3(0.30, 0.29, 0.27), vec3(0.50, 0.47, 0.43), id);

            stone = mix(stone, vec3(0.30, 0.20, 0.13), smoothstep(0.62, 0.95, hash12(vec2(id, 4.1))));

            float rusty = 1.0 - smoothstep(3.0, 9.0, abs(abs(across) - 10.0));

            stone = mix(stone, stone * vec3(0.72, 0.5, 0.36), 0.55 * rusty);

            vec3 mean = vec3(0.33, 0.31, 0.285) * mix(1.0, 0.8, rusty);

            albedo = mix(mean, stone * mix(0.28, 1.0, gap), fine);

            rough = 0.82;

            n = bumped(n, fine * (0.2 * gap * (1.0 - 0.5 * nearest) + 0.12 * id) * uTubeRadius / 20.0);
        }
        else if (part < 0.25)
        {
            // a sleeper: creosoted timber, nearly black, split and weathered
            float id = vTexcoord.z;

            float grainPlace = across * 9.0 + 2.0 * noise2(vec2(along * 0.4, id));

            float grain = noise2(vec2(along * 0.32 + id * 3.1, grainPlace));
            float split = smoothstep(0.7, 0.78, noise2(vec2(along * 0.09 + id, grainPlace * 0.6)));

            vec3 timber = mix(vec3(0.075, 0.055, 0.042), vec3(0.17, 0.13, 0.095), 0.6 * hash12(vec2(id, 2.2)) + 0.4 * grain);

            albedo = timber * mix(1.0, 0.45, split * fine);

            rough = 0.72;

            n = bumped(n, fine * (0.12 * (grain - 0.5) - 0.2 * split) * uTubeRadius / 20.0);
        }
        else if (part < 0.32)
        {
            // the top of a rail: bright steel polished by wheels
            float scratch = noise2(vec2(along * 0.5, across * 16.0));

            float length144 = vTexcoord.z;
            float local = mod(along, length144);

            float joint = 1.0 - smoothstep(0.15, 0.5, min(local, length144 - local));

            albedo = mix(vec3(0.62, 0.61, 0.60), vec3(0.03), joint);
            metal = 1.0 - joint;
            rough = 0.2 + 0.1 * scratch + 0.5 * joint;
        }
        else if (part < 0.38)
        {
            // a rail's side: rust, darker and redder in patches
            float patchy = noise2(vec2(along * 0.06, across * 0.8)) * 0.6 + noise2(vec2(along * 0.31, across * 3.1)) * 0.4;

            albedo = mix(vec3(0.20, 0.095, 0.05), vec3(0.33, 0.17, 0.08), patchy);

            rough = 0.85;
        }
        else if (part < 0.45)
        {
            albedo = vec3(0.055, 0.05, 0.048);
            rough = 0.9;
        }
        else
        {
            /*
             * The steel of a bridge: plate girders painted a dull grey-green
             * long ago, with rows of rivets and rust running down from them.
             */
            float h = vWorldPosition.y / uTubeRadius * 20.0;

            float cellU = mod(along, 7.0) - 3.5;
            float rowA = mod(h, 4.6) - 2.3;

            float rivet = 1.0 - smoothstep(0.55, 0.95, length(vec2(cellU, rowA)));

            float rust = noise2(vec2(along * 0.09, h * 0.14)) * 0.65 + noise2(vec2(along * 0.5, h * 0.5)) * 0.35;

            albedo = mix(vec3(0.075, 0.095, 0.085), vec3(0.17, 0.09, 0.05), smoothstep(0.52, 0.8, rust));

            rough = 0.6;

            n = bumped(n, fine * 0.35 * rivet * uTubeRadius / 20.0);
        }
    }
    else if (uMaterial == 7)
    {
        /*
         * The piers and abutments: blocks of grey-gold stone in courses, each
         * block its own shade, with weathered mortar between them. vTexcoord
         * is the place on the face (mm).
         */
        float h = vWorldPosition.y / uTubeRadius * 20.0;

        float wide = vTexcoord.x + vTexcoord.y;

        float course = floor(h / 6.4);

        float shifted = wide + 5.5 * mod(course, 2.0);

        float block = floor(shifted / 11.0);

        float inH = abs(fract(h / 6.4) - 0.5) * 6.4;
        float inW = abs(fract(shifted / 11.0) - 0.5) * 11.0;

        float joint = max(smoothstep(2.65, 3.1, inH), smoothstep(5.05, 5.45, inW));

        float id = hash12(vec2(block, course));

        float dressed = noise2(vec2(wide * 0.6 + id * 9.0, h * 0.8));
        float lichen = smoothstep(0.58, 0.78, noise2(vec2(wide * 0.07, h * 0.09)));

        vec3 stone = mix(vec3(0.27, 0.235, 0.19), vec3(0.42, 0.37, 0.30), id);

        stone = mix(stone, vec3(0.12, 0.13, 0.09), 0.5 * lichen);

        albedo = mix(stone * (0.86 + 0.28 * dressed), vec3(0.07, 0.065, 0.06), joint);

        rough = 0.86;

        n = bumped(n, (0.09 * (dressed - 0.5) - 0.5 * joint) * uTubeRadius / 20.0);
    }
