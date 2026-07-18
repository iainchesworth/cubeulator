// Cel-shaded fragment snippet for CubeFaceMaterial's "Toon outline" style:
// quantised diffuse bands plus a rim highlight, instead of PBR shading. See
// https://doc.qt.io/qt-6/qtquick3d-custom.html for the MAIN()/AMBIENT_LIGHT()/
// DIRECTIONAL_LIGHT() shader model this plugs into.

void MAIN()
{
    ROUGHNESS = 1.0;
    METALNESS = 0.0;
    SPECULAR_AMOUNT = 0.0;
    BASE_COLOR = uBaseColor;

    vec3 viewDir = normalize(CAMERA_POSITION - VAR_WORLD_POSITION);
    float rim = 1.0 - max(dot(normalize(NORMAL), viewDir), 0.0);
    EMISSIVE_COLOR = uRimColor.rgb * pow(rim, uRimPower);
}

void AMBIENT_LIGHT()
{
    DIFFUSE += uBaseColor.rgb * TOTAL_AMBIENT_COLOR;
}

void DIRECTIONAL_LIGHT()
{
    float ndotl = max(dot(normalize(NORMAL), TO_LIGHT_DIR), 0.0);
    float band = ndotl > 0.85 ? 1.0 : (ndotl > 0.4 ? 0.6 : 0.25);
    DIFFUSE += uBaseColor.rgb * LIGHT_COLOR * SHADOW_CONTRIB * band;
}
