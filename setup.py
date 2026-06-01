import os
import urllib.request
import zipfile
import sys

def progress_bar(block_num, block_size, total_size):

    downloaded = block_num * block_size
    percent = int(100 * downloaded / total_size)

    bar = '=' * (percent // 2) + '-' * (50 - (percent // 2))

    sys.stdout.write(f'\r[{bar}] {percent}%')
    sys.stdout.flush()

def download_and_extract_assets():
    target_dir = os.path.join("blk_engine", "assets")
    zip_path = "assets.zip"
    url = "https://benny-wilson.com/blk_storage/gaussian_splats.zip"

    if not os.path.exists(target_dir):
        os.makedirs(target_dir)

    print(f"Downloading {url} to {target_dir}...")
    
    urllib.request.urlretrieve(url, zip_path, reporthook=progress_bar)
    print("\nDownload complete.")
    
    print("Extracting...")
    with zipfile.ZipFile(zip_path, 'r') as zip_ref:
        zip_ref.extractall(target_dir)
    
    os.remove(zip_path)
    print("Assets successfully installed!")

if __name__ == "__main__":
    download_and_extract_assets()