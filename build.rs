fn main() {
    if cfg!(target_os = "windows") {
        let mut res = winres::WindowsResource::new();
        
        res.set("ProductName", "AutoRecordOBS");
        res.set("FileDescription", "AutoRecordOBS Tray Application");
        res.set("OriginalFilename", "AutoRecordOBS.exe");

        res.set_icon("assets/icon.ico");

        res.compile().unwrap();
    }
}