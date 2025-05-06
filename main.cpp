#include <chrono> // For sleep
#include <curl/curl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <unistd.h>
using json = nlohmann::json;

#define TOO_MANY_REQUESTS_SIZE 17

#define JSON_SUCCESS 0
#define JSON_CURL_EASY_FAILED -1
#define JSON_302 -2

class URL {
    public:
        std::string address;
        std::string folder;
        uint64_t timer;
        std::string board;
        std::string jsonURL;
        std::string jsonContents;
        json jsonData;

        URL(std::string add, std::string fold, uint64_t time) {
            address = add;
            folder = fold;
            timer = time;
        };
};

size_t curl_write_to_string(void *contents, size_t size, size_t nmemb,
        std::string *s) {
    // https://stackoverflow.com/a/36401787
    size_t newLength = size * nmemb;
    try {
        s->append((char *)contents, newLength);
    } catch (std::bad_alloc &e) {
        // handle memory problem
        return 0;
    }
    return newLength;
}

size_t write_data(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    size_t written = fwrite(ptr, size, nmemb, stream);
    return written;
}

int8_t makeJSONURL(URL &url) {
    // Find all slashes in URL (such as https://boards.4chan.org/c/thread/4322780)
    std::vector<int16_t> slashes;
    for (int i = 0; i < url.address.length(); ++i) {
        if (url.address[i] == '/') {
            slashes.push_back(i);
        }
    }
    if (slashes.size() != 5) {
        std::cout << "Malformed address\n";
        return -1;
    }

    // Slicing out the URL to make the json URL
    // (https://a.4cdn.org/{board}/thread/{threadNumber}.json)
    std::string board =
        url.address.substr(slashes[2] + 1, slashes[3] - slashes[2] - 1);
    std::string thread =
        url.address.substr(slashes[4] + 1, url.address.length() - slashes[4]);
    std::string jsonURL =
        "https://a.4cdn.org/" + board + "/thread/" + thread + ".json";

    url.board = board;
    url.jsonURL = jsonURL;

    return 0;
}

int32_t downloadJSON(URL &url) {
    // Download JSON
    CURL *curl;
    CURLcode res;
    curl = curl_easy_init();
    if (curl) {
        // If download fails, wait a second then do it again.
        do {
            curl_easy_setopt(curl, CURLOPT_URL, url.jsonURL.c_str());
            curl_easy_setopt(curl, CURLOPT_USERAGENT,
                    "Dark Secret Ninja/1.0"); // This is required.
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_to_string);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &url.jsonContents);
            res = curl_easy_perform(curl);
            curl_easy_cleanup(curl);

            // Couldn't download json file
            if (res) {
                std::cout << "json file curl response failed" << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        } while (res);
    } else {
        std::cout << "json file curl_easy_init() failed" << std::endl;
        return JSON_CURL_EASY_FAILED;
    }

    // Check if JSON file isn't just a 302 found
    std::string comp_string = "<html>"; // JSON wouldn't start with this
    for (uint8_t i = 0; i < comp_string.length(); ++i) {
        if (url.jsonContents[i] != comp_string[i]) {
            return JSON_SUCCESS;
        }
    }

    return JSON_302;
}

int8_t getImageFilenames(URL &url,
        std::map<std::string, uint64_t> &imageFilenames, bool bypass) {
    // Stop downloading if thread is archived
    if (url.jsonData["posts"][0]["archived"] == 1 && bypass == false) {
        // std::cout << url.jsonData["posts"][0]["archived"] << std::endl;
        std::string command =
            "notify-send \"" + url.board + " " + url.folder + " became archived\"";
        std::system(command.c_str());
        std::cout << "404" << std::endl;
        return -1;
    } else {
        // Get all image names into a vector
        for (auto &[key, val] : url.jsonData["posts"].items()) {
            if (val.contains("tim")) {
                imageFilenames[val["tim"].dump() + val["ext"].get<std::string>()] =
                    val["fsize"];
            }
        }
        return 0;
    }
}

std::map<std::string, uint64_t>
compareLocalFiles(std::string &folderName,
        std::map<std::string, uint64_t> &imageFilenames) {
    // Check with local files
    // Remove any already downloaded images
    // This only checks filenames so uncompleted downloaded files will persist
    // I could check hash but that's more intensive
    // TODO: Maybe just check filesizes?
    for (auto &existingFile : std::filesystem::directory_iterator{folderName}) {
        // Couldn't find a way to pop element from vector using range-based loop
        for (auto it = imageFilenames.begin(); it != imageFilenames.end(); ++it) {
            // Check filename first
            if (existingFile.path() == (folderName + "/" + it->first)) {
                imageFilenames.erase(it);
                break;
                // Then check filesize (API isn't accurate so don't do this)
                // if (existingFile.file_size() == it->second) {
                // }
            }
        }
    }

    return imageFilenames;
}

uint64_t downloadFiles(URL &url,
        std::map<std::string, uint64_t> &imageFilenames,
        uint64_t *count) {
    // Download images
    auto it = imageFilenames.begin();
    while (it != imageFilenames.end()) {
        // for (auto &[filename, _] : imageFilenames) {
        CURL *curl;
        FILE *fp;
        CURLcode res;
        std::string filename = it->first;

        curl = curl_easy_init();
        if (curl) {
            // Make download URL and download image.
            std::string filePath = url.folder + "/" + filename;
            fp = std::fopen(filePath.c_str(), "wb");
            std::string imageURL =
                std::string("https://i.4cdn.org/") + url.board + "/" + filename;

            // If download fails, wait a second then do it again.
            do {
                // Download image
                curl_easy_setopt(curl, CURLOPT_URL, imageURL.c_str());
                curl_easy_setopt(curl, CURLOPT_USERAGENT, "Dark Secret Ninja/1.0"); // This is required.
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
                res = curl_easy_perform(curl);

                if (res) {
                    std::cout << "image file curl response failed" << std::endl;
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                } else {
                    ++*count;
                }
            } while (res);

            curl_easy_cleanup(curl);
            fclose(fp);

            // Check to see if we're blocked (17 bytes which is "Too many requests")
            curl_off_t sizeDownloaded;
            res = curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD_T, &sizeDownloaded);
            if (sizeDownloaded == TOO_MANY_REQUESTS_SIZE) {
                std::cout << "Too many requests, waiting 10 seconds\n";
                sleep(10);
            } else {
                ++it; // Go to the next file
            }

            // ++it;
        }
        // sleep(1); // 4chan API time between requests
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
    }

    int main(int argc, char *argv[]) {
        std::string threadURL, folderName;
        uint64_t sleepTime;
        if (argc >= 4) {
            // Parse argv
            threadURL = argv[1];
            folderName = argv[2];
            sleepTime = std::stoi(argv[3]);
        } else {
            std::cout << "Provide URL, folder, and sleep time\n";
            return 0;
        }

        // In case if we still wanna try downloading a 404 thread
        bool bypass = false;
        if (argc == 5) {
            bypass = true;
        }

        // Check if directory exists, make if not
        if (!std::filesystem::exists(folderName)) {
            std::filesystem::create_directory(folderName);
        }

        int32_t err = 0;
        URL url(threadURL, folderName, sleepTime);

        // JSON
        std::string command;
        err = makeJSONURL(url);
        if (err) {
            std::cout << "makeJSONURL failed" << std::endl;
        }

        std::map<std::string, uint64_t> imageFilenames;
        while (1) {
            // Download JSON and check for errors
            err = downloadJSON(url);
            switch (err) {
                case JSON_302:
                    command = "notify-send \"" + url.board + " " + url.folder +
                        " became archived (302)\"";
                    std::system(command.c_str());
                    std::cout << "302" << std::endl;
                    return 0;
                case JSON_CURL_EASY_FAILED:
                    command = "notify-send \"" + url.board + " " + url.folder +
                        " became archived (JSON_CURL_EASY_FAILED)\"";
                    std::system(command.c_str());
                    std::cout << "JSON_CURL_EASY_FAILED" << std::endl;
                    return 0;
            }

            if (url.jsonContents == "") { // Another case when thread is 404
                command = "notify-send \"" + url.board + " " + url.folder +
                    " became archived\"";
                std::system(command.c_str());
                std::cout << "404" << std::endl;
                return 0;
            }
            url.jsonData = json::parse(url.jsonContents);

            // Images
            err = getImageFilenames(url, imageFilenames, bypass); // [filename, fsize]
            if (err) {
                return -1;
            }

            imageFilenames = compareLocalFiles(url.folder, imageFilenames);
            if (err) {
                return -1;
            }

            uint64_t count = 0;
            err = downloadFiles(url, imageFilenames, &count);
            if (err) {
                return -1;
            }

            // Housekeeping
            url.jsonData.clear();
            url.jsonContents.clear();
            imageFilenames.clear();
            // json().swap(url.jsonData);
            // url.jsonContents = std::string();
            // std::map<std::string, uint64_t>().swap(imageFilenames);

            // Sleep and run again
            std::cout << "Downloaded " << count << " files\n";
            std::this_thread::sleep_for(std::chrono::seconds(url.timer));
        }

        return 0;
    }
