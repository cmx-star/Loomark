use serde::Serialize;
use std::fs::{self, File};
use std::io::{BufRead, BufReader, Read};
use std::path::Path;
use std::time::Instant;

const FULL_EDITOR_LIMIT_BYTES: u64 = 10 * 1024 * 1024;
const MAX_SUPPORTED_BYTES: u64 = 50 * 1024 * 1024;
const MAX_DIRECTORY_ENTRIES: usize = 1_000;
const PREVIEW_LIMIT_BYTES: usize = 256 * 1024;

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct DocumentInspection {
    path: String,
    byte_size: u64,
    line_count: u64,
    longest_line_bytes: u64,
    preflight_milliseconds: f64,
    strategy: &'static str,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct LoadedDocument {
    #[serde(flatten)]
    inspection: DocumentInspection,
    content: String,
    read_milliseconds: f64,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct DirectoryEntry {
    is_directory: bool,
    name: String,
    path: String,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct DirectoryListing {
    entries: Vec<DirectoryEntry>,
    parent_path: Option<String>,
    path: String,
    truncated: bool,
}

fn classify_document(byte_size: u64) -> &'static str {
    if byte_size <= FULL_EDITOR_LIMIT_BYTES {
        "full"
    } else if byte_size <= MAX_SUPPORTED_BYTES {
        "progressive"
    } else {
        "unsupported"
    }
}

fn is_markdown_file(path: &Path) -> bool {
    matches!(
        path.extension().and_then(|extension| extension.to_str()),
        Some(extension)
            if matches!(
                extension.to_ascii_lowercase().as_str(),
                "md" | "markdown" | "mdown" | "mkdn" | "mdtxt"
            )
    )
}

fn inspect(path: &str) -> Result<DocumentInspection, String> {
    let started_at = Instant::now();
    let file_path = Path::new(path);
    let metadata =
        fs::metadata(file_path).map_err(|error| format!("Could not inspect file: {error}"))?;
    if !metadata.is_file() {
        return Err("The selected path is not a file.".to_string());
    }

    let mut reader = BufReader::new(
        File::open(file_path).map_err(|error| format!("Could not open file: {error}"))?,
    );
    let mut line_count = 0_u64;
    let mut longest_line_bytes = 0_u64;
    let mut buffer = Vec::new();
    loop {
        buffer.clear();
        let read = reader
            .read_until(b'\n', &mut buffer)
            .map_err(|error| format!("Could not scan file: {error}"))?;
        if read == 0 {
            break;
        }
        line_count += 1;
        let without_newline = buffer.strip_suffix(b"\n").unwrap_or(&buffer);
        let line_bytes = without_newline
            .strip_suffix(b"\r")
            .unwrap_or(without_newline);
        longest_line_bytes = longest_line_bytes.max(line_bytes.len() as u64);
    }

    Ok(DocumentInspection {
        path: path.to_string(),
        byte_size: metadata.len(),
        line_count,
        longest_line_bytes,
        preflight_milliseconds: started_at.elapsed().as_secs_f64() * 1000.0,
        strategy: classify_document(metadata.len()),
    })
}

#[tauri::command]
fn inspect_document(path: String) -> Result<DocumentInspection, String> {
    inspect(&path)
}

#[tauri::command]
fn read_document_preview(path: String) -> Result<String, String> {
    let mut file = File::open(&path).map_err(|error| format!("Could not open file: {error}"))?;
    let mut bytes = vec![0; PREVIEW_LIMIT_BYTES];
    let read = file
        .read(&mut bytes)
        .map_err(|error| format!("Could not read file preview: {error}"))?;
    bytes.truncate(read);
    Ok(String::from_utf8_lossy(&bytes).into_owned())
}

#[tauri::command]
fn read_document(path: String) -> Result<LoadedDocument, String> {
    let inspection = inspect(&path)?;
    if inspection.strategy == "unsupported" {
        return Err("Files above 50 MiB are outside the M0 supported range.".to_string());
    }
    let started_at = Instant::now();
    let content = fs::read_to_string(&path)
        .map_err(|error| format!("Could not read UTF-8 Markdown: {error}"))?;
    Ok(LoadedDocument {
        inspection,
        content,
        read_milliseconds: started_at.elapsed().as_secs_f64() * 1000.0,
    })
}

#[tauri::command]
fn save_document(path: String, content: String) -> Result<(), String> {
    fs::write(&path, content).map_err(|error| format!("Could not save Markdown: {error}"))
}

fn list_markdown_directory(directory_path: &Path) -> Result<DirectoryListing, String> {
    if !directory_path.is_dir() {
        return Err("The selected path is not a directory.".to_string());
    }
    let directory = fs::read_dir(directory_path)
        .map_err(|error| format!("Could not read document directory: {error}"))?;
    let mut entries = Vec::new();
    let mut truncated = false;

    for entry in directory {
        let entry = entry.map_err(|error| format!("Could not read directory entry: {error}"))?;
        let entry_name = entry.file_name();
        if entry_name.to_string_lossy().starts_with('.') {
            continue;
        }
        let entry_path = entry.path();
        let is_directory = entry_path.is_dir();
        if !is_directory && (!entry_path.is_file() || !is_markdown_file(&entry_path)) {
            continue;
        }
        if entries.len() == MAX_DIRECTORY_ENTRIES {
            truncated = true;
            break;
        }
        entries.push(DirectoryEntry {
            is_directory,
            name: entry_name.to_string_lossy().into_owned(),
            path: entry_path.to_string_lossy().into_owned(),
        });
    }

    entries.sort_by(|left, right| {
        right
            .is_directory
            .cmp(&left.is_directory)
            .then_with(|| left.name.to_lowercase().cmp(&right.name.to_lowercase()))
    });
    Ok(DirectoryListing {
        entries,
        parent_path: directory_path
            .parent()
            .filter(|parent| !parent.as_os_str().is_empty())
            .map(|parent| parent.to_string_lossy().into_owned()),
        path: directory_path.to_string_lossy().into_owned(),
        truncated,
    })
}

#[tauri::command]
fn list_markdown_siblings(path: String) -> Result<DirectoryListing, String> {
    let document_path = Path::new(&path);
    let directory_path = document_path
        .parent()
        .filter(|directory| !directory.as_os_str().is_empty())
        .ok_or_else(|| "The selected document has no parent directory.".to_string())?;
    list_markdown_directory(directory_path)
}

#[tauri::command]
fn browse_markdown_directory(path: String) -> Result<DirectoryListing, String> {
    list_markdown_directory(Path::new(&path))
}

pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .invoke_handler(tauri::generate_handler![
            inspect_document,
            read_document_preview,
            read_document,
            save_document,
            list_markdown_siblings,
            browse_markdown_directory
        ])
        .run(tauri::generate_context!())
        .expect("error while running Loomark");
}

#[cfg(test)]
mod tests {
    use super::{classify_document, is_markdown_file, FULL_EDITOR_LIMIT_BYTES, MAX_SUPPORTED_BYTES};
    use std::path::Path;
    use std::{env, fs};

    #[test]
    fn classifies_supported_size_boundaries() {
        assert_eq!(classify_document(FULL_EDITOR_LIMIT_BYTES), "full");
        assert_eq!(
            classify_document(FULL_EDITOR_LIMIT_BYTES + 1),
            "progressive"
        );
        assert_eq!(classify_document(MAX_SUPPORTED_BYTES), "progressive");
        assert_eq!(classify_document(MAX_SUPPORTED_BYTES + 1), "unsupported");
    }

    #[test]
    fn recognizes_supported_markdown_extensions_case_insensitively() {
        assert!(is_markdown_file(Path::new("notes.MD")));
        assert!(is_markdown_file(Path::new("notes.markdown")));
        assert!(!is_markdown_file(Path::new("notes.txt")));
    }

    #[test]
    fn lists_only_markdown_siblings() {
        let directory = env::temp_dir().join(format!("loomark-directory-test-{}", std::process::id()));
        fs::create_dir_all(&directory).unwrap();
        let active_path = directory.join("active.md");
        fs::write(&active_path, "# Active").unwrap();
        fs::write(directory.join("second.MD"), "# Second").unwrap();
        fs::write(directory.join("ignored.txt"), "Ignore").unwrap();
        fs::write(directory.join(".hidden.md"), "# Hidden").unwrap();

        let nested_directory = directory.join("nested");
        fs::create_dir(&nested_directory).unwrap();
        fs::write(nested_directory.join("nested.md"), "# Nested").unwrap();
        let hidden_directory = directory.join(".hidden");
        fs::create_dir(&hidden_directory).unwrap();
        fs::write(hidden_directory.join("hidden.md"), "# Hidden").unwrap();

        let listing = super::list_markdown_siblings(active_path.to_string_lossy().into_owned()).unwrap();
        let names = listing.entries.iter().map(|entry| entry.name.as_str()).collect::<Vec<_>>();

        assert_eq!(names, vec!["nested", "active.md", "second.MD"]);
        assert!(!names.contains(&".hidden"));
        assert!(!names.contains(&".hidden.md"));
        assert!(listing.entries[0].is_directory);
        assert_eq!(listing.parent_path, directory.parent().map(|path| path.to_string_lossy().into_owned()));

        let nested_listing = super::browse_markdown_directory(nested_directory.to_string_lossy().into_owned()).unwrap();
        assert_eq!(nested_listing.entries.len(), 1);
        assert_eq!(nested_listing.entries[0].name, "nested.md");
        assert!(!nested_listing.entries[0].is_directory);
        fs::remove_dir_all(directory).unwrap();
    }
}
