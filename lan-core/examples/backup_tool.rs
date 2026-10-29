//! Test helper for the Rust<->C backup interop check (editions/portable/tests/backup_interop.py).
//!   backup_tool export <base> <password> <out>   (creates an identity in <base> if missing)
//!   backup_tool import <base> <password> <in>
//!   backup_tool show <base> [fp]                 -> "FP <fp>" and, for [fp], "CONTACT <name>\t<trust>\t<muted>\t<addr0>"
use lan_core::{backup, config::ConfigPaths, contacts::ContactStore, identity::Identity};

fn main() -> anyhow::Result<()> {
    let a: Vec<String> = std::env::args().collect();
    anyhow::ensure!(
        a.len() >= 3,
        "usage: backup_tool export|import|show <base> ..."
    );
    let p = ConfigPaths::from_base(&a[2]);
    match a[1].as_str() {
        "export" => {
            Identity::load_or_create(&p)?;
            backup::export_backup(&p, &a[3], std::path::Path::new(&a[4]))?;
        }
        "import" => backup::import_backup(&p, &a[3], std::path::Path::new(&a[4]))?,
        "show" => {
            println!("FP {}", Identity::load(&p)?.fingerprint().as_str());
            if let Some(fp) = a.get(3) {
                let s = ContactStore::load(&p)?;
                let c = s
                    .get(fp)
                    .ok_or_else(|| anyhow::anyhow!("no contact {fp}"))?;
                println!(
                    "CONTACT {}\t{:?}\t{}\t{}",
                    c.name,
                    c.trust,
                    c.muted,
                    c.last_addrs.first().cloned().unwrap_or_default()
                );
            }
        }
        _ => anyhow::bail!("unknown command"),
    }
    Ok(())
}
