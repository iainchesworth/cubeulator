// Fragment snippet for Borderlands Cel-Shading's sticker material: hard
// 2-step diffuse banding (no soft gradient, no colour rim - the ink-outline
// mesh alongside this material provides the silhouette line instead). See
// https://doc.qt.io/qt-6/qtquick3d-custom.html for the MAIN()/AMBIENT_LIGHT()/
// DIRECTIONAL_LIGHT() shader model this plugs into.

void MAIN()
{
    ROUGHNESS = 1.0;
    METALNESS = 0.0;
    SPECULAR_AMOUNT = 0.0;
    BASE_COLOR = uBaseColor;
}

void AMBIENT_LIGHT()
{
    DIFFUSE += uBaseColor.rgb * TOTAL_AMBIENT_COLOR;
}

void DIRECTIONAL_LIGHT()
{
    float ndotl = max(dot(normalize(NORMAL), TO_LIGHT_DIR), 0.0);
    float band = ndotl > 0.5 ? 1.0 : 0.35;
    DIFFUSE += uBaseColor.rgb * LIGHT_COLOR * SHADOW_CONTRIB * band;
}
