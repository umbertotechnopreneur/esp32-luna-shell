// No parameters: Propagate the selected ESP-IDF build environment to Rust.
fn main() {
    embuild::espidf::sysenv::output();
}
