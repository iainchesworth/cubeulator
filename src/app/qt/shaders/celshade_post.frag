// Post-processing pass for Borderlands Cel-Shading: a halftone dot overlay
// plus a light desaturation/contrast push, giving the comic-ink look the
// hard-banded cel material and inverted-hull outline alone don't cover.
// Vignette itself comes from ExtendedSceneEnvironment's own built-in
// property, not this shader.

void MAIN()
{
    vec4 color = texture(INPUT, INPUT_UV);

    float luma = dot(color.rgb, vec3(0.299, 0.587, 0.114));
    vec3 pushed = mix(vec3(luma), color.rgb, 0.75);
    pushed = (pushed - 0.5) * 1.15 + 0.5;

    vec2 pixel = INPUT_UV * INPUT_SIZE;
    float cell = 5.0;
    vec2 cellCenter = (floor(pixel / cell) + 0.5) * cell;
    float dist = length(pixel - cellCenter) / (cell * 0.5);
    float dotShape = 1.0 - smoothstep(luma - 0.06, luma + 0.06, dist);
    vec3 halftoned = mix(pushed * 0.85, pushed, dotShape);

    FRAGCOLOR = vec4(halftoned, color.a);
}
