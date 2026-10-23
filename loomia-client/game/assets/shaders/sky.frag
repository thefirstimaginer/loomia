// Céu do Loomia — gradiente entre o zênite e o horizonte definidos pelo mapa.
//
// Contrato de skybox da Ruzit (docs/shaders.html): o prelúdio injeta `VsOut`
// (`uv` de 0 no topo a 1 na base) e `ruzit_apply_alpha`. Os parâmetros são
// declarados com `// @ruzit param` — a ordem define os slots (`p(0)`, ...).

// @ruzit param zenith_r
// @ruzit param zenith_g
// @ruzit param zenith_b
// @ruzit param horizon_r
// @ruzit param horizon_g
// @ruzit param horizon_b
// @ruzit param horizon_height

@fragment
fn fs_main(in: VsOut) -> @location(0) vec4<f32> {
    let zenith = vec3<f32>(p(0u), p(1u), p(2u));
    let horizon = vec3<f32>(p(3u), p(4u), p(5u));
    let horizon_height = p(6u);

    // Transição suave do zênite (topo) para o horizonte (base).
    let t = smoothstep(horizon_height, 1.0, in.uv.y);
    let rgb = mix(zenith, horizon, t);
    return ruzit_apply_alpha(rgb);
}
