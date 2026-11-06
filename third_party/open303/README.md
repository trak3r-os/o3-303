# Open303 (reference copy)

Source: https://github.com/RobinSchmidt/Open303, commit `313bf0d9ade7c1dcb6b3a74f5ea1780a29d70074` (2024-03-29), `Source/DSPCode/`, MIT License,
Copyright (c) 2009 Robin Schmidt (www.rs-met.com) (`LICENSE`). The files are byte-for-byte the upstream ones (`SHA256SUMS`).

The oracle of the ACID voice (units/acid_voice/tests/acid_voice_oracle_test.cpp): the host tests build these files and compare the voice's ladder
(`TeeBeeFilter`, TB_303 mode), its cutoff law (`Open303::calculateEnvModScalerAndOffset`), its amplifier and its high-pass, all-pass and notch
chain with Open303's. Nothing in this directory is linked into an image. The sources need `<climits>` and `<cstring>` first (INT_MAX, memcpy): the
test target adds `-include climits -include cstring` instead of changing a file.
