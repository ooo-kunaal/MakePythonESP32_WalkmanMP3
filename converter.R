library(fs)

in_dir  <- "C:/Users/KUNAL/Downloads/Transfer/Osho"
out_dir <- path(in_dir, "wav_44100_16bit")
dir_create(out_dir)

ffmpeg_exe <- "C:/Users/KUNAL/AppData/Local/Microsoft/WinGet/Packages/Gyan.FFmpeg_Microsoft.Winget.Source_8wekyb3d8bbwe/ffmpeg-8.0.1-full_build/bin/ffmpeg.exe"
ffmpeg_exe <- normalizePath(ffmpeg_exe, winslash = "/", mustWork = TRUE)

safe_name <- function(x){
  x <- gsub("[<>:\"/\\\\|?*]+","_",x)
  x <- gsub("\\s+","_",x)
  gsub("_+","_",x)
}

run_ffmpeg <- function(in_file, out_file) {
  in_file  <- normalizePath(in_file,  winslash = "/", mustWork = TRUE)
  out_file <- normalizePath(out_file, winslash = "/", mustWork = FALSE)
  
  args <- c(
    "-hide_banner", "-y",
    "-i", shQuote(in_file, type = "cmd"),
    "-vn",
    "-ac", "2",
    "-ar", "44100",
    "-c:a", "pcm_s16le",
    shQuote(out_file, type = "cmd")
  )
  
  log <- system2(ffmpeg_exe, args = args, stdout = TRUE, stderr = TRUE)
  st <- attr(log, "status"); if (is.null(st)) st <- 0L
  if (st != 0L) cat(paste(tail(log, 80), collapse = "\n"), "\n")
  st
}

ext_ok <- c("mp3","m4a","aac","flac","ogg","wma","wav","aiff","opus","mka","mp4","webm")
files <- dir_ls(in_dir, type="file", recurse=FALSE)
files <- files[tolower(path_ext(files)) %in% ext_ok]
files <- files[!grepl("(/|\\\\)wav_44100_16bit(/|\\\\)", files)]

for (i in seq_along(files)) {
  f <- files[i]
  base <- safe_name(path_ext_remove(path_file(f)))
  out <- path(out_dir, paste0(base, ".wav"))
  if (file_exists(out)) next
  message(sprintf("[%d/%d] %s -> %s", i, length(files), path_file(f), path_file(out)))
  st <- run_ffmpeg(f, out)
  if (st != 0L) message("FAILED: ", path_file(f))
}
