//! Cross-implementation vectors for sanitize_filename (Rust lan-core is the reference; the C core in
//! editions/portable checks the same file with tests/c_unit_test.c). Regenerate the expected file with
//! `LSM_GEN_VECTORS=1 cargo test -p lan-core --test filename_vectors` after an intended change.
use lan_core::engine::sanitize_filename;

#[test]
fn filename_vectors() {
    let dir = concat!(env!("CARGO_MANIFEST_DIR"), "/tests/data");
    let inputs: Vec<String> = serde_json::from_str(&std::fs::read_to_string(format!("{dir}/filename_inputs.json")).unwrap()).unwrap();
    let got: Vec<[String; 2]> = inputs.iter().map(|i| [i.clone(), sanitize_filename(i)]).collect();
    let path = format!("{dir}/filename_vectors.json");
    if std::env::var("LSM_GEN_VECTORS").is_ok() {
        std::fs::write(&path, serde_json::to_string_pretty(&got).unwrap()).unwrap();
    }
    let want: Vec<[String; 2]> = serde_json::from_str(&std::fs::read_to_string(&path).unwrap()).unwrap();
    assert_eq!(got, want);
    for [_, out] in &got {
        assert!(!out.is_empty() && !out.contains(['/', '\\']) && out != "." && out != "..", "unsafe name {out:?}");
        assert!(!out.starts_with('.'), "hidden/dot name {out:?}");
    }
}
