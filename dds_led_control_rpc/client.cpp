#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include <csignal>
#include <random>
#include <map>

#include "LedControl.hpp"

/* Include the C++ DDS API. */
#include "dds/dds.hpp"


using namespace std::chrono_literals;


class LedClient 
{
private:
    dds::domain::DomainParticipant participant;
    dds::topic::Topic<led_control::LedRequest> request_topic;
    dds::topic::Topic<led_control::LedResponse> response_topic;
    dds::pub::Publisher publisher;
    dds::sub::Subscriber subscriber;
    dds::pub::DataWriter<led_control::LedRequest> request_writer;
    dds::sub::DataReader<led_control::LedResponse> response_reader;
    
    std::atomic<bool> running{true};
    unsigned long request_counter{0};
    std::map<unsigned long, std::chrono::steady_clock::time_point> pending_requests;
    
    std::random_device rd;
    std::mt19937 gen{rd()};
    std::uniform_int_distribution<> color_dist{0, 2};
    std::uniform_int_distribution<> state_dist{0, 1};
    
    const char* colorToString(led_control::LedColor color) 
    {
        switch(color) 
        {
            case led_control::LedColor::RED: return "RED";
            case led_control::LedColor::GREEN: return "GREEN";
            case led_control::LedColor::BLUE: return "BLUE";
            default: return "UNKNOWN";
        }
    }
    
    void sendRequest(led_control::LedColor color, bool state) 
    {
        led_control::LedRequest request;
        request.color(color);
        request.state(state);
        request.request_id(++request_counter);
        
        std::cout << "Sending request: "
                  << colorToString(color) 
                  << " -> " << (state ? "ON" : "OFF")
                  << " (ID: " << request.request_id() << ")" << std::endl;
        
        request_writer.write(request);
        pending_requests[request.request_id()] = std::chrono::steady_clock::now();
    }
    
    void checkResponses() 
    {
        auto samples = response_reader.select()
            .state(dds::sub::status::DataState::new_data())
            .take();
        
        for (const auto& sample : samples) 
        {
            if(sample.info().valid()) 
            {
                const auto& response = sample.data();
                auto it = pending_requests.find(response.request_id());
                
                if(it != pending_requests.end()) 
                {
                    auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - it->second).count();
                    
                    std::cout << "\nReceived response for request ID: " << response.request_id() << std::endl;
                    std::cout << "  Success: " << (response.success() ? "Yes" : "No") << std::endl;
                    std::cout << "  Message: " << response.message() << std::endl;
                    std::cout << "  Color: " << colorToString(response.color()) << std::endl;
                    std::cout << "  State: " << (response.state() ? "ON" : "OFF") << std::endl;
                    std::cout << "  Latency: " << latency << "ms" << std::endl;
                    
                    pending_requests.erase(it);
                }
            }
        }
        
        // Check for timeout (5 seconds) - 'erase' request if timed out:
        auto now = std::chrono::steady_clock::now();

        for (auto it = pending_requests.begin(); it != pending_requests.end(); ) 
        {
            if (now - it->second > 5s) 
            {
                std::cerr << "Timeout for request ID: " << it->first << std::endl;
                it = pending_requests.erase(it);
            } 
            else 
            {
                ++it;
            }
        }
    }
    
    void sendRandomRequest() 
    {
        led_control::LedColor color = static_cast<led_control::LedColor>(color_dist(gen));
        bool state = state_dist(gen) == 1;
        sendRequest(color, state);
    }

public:
    LedClient(int domain_id = 0) 
        : participant(domain_id),
          request_topic(participant, "led_control_requests"),
          response_topic(participant, "led_control_responses"),
          publisher(participant),
          subscriber(participant),
          request_writer(publisher, request_topic),
          response_reader(subscriber, response_topic) {
        
        // Wait for server to be available
        dds::core::status::PublicationMatchedStatus status;
        do 
        {
            status = request_writer.publication_matched_status();
            std::this_thread::sleep_for(100ms);
        } 
        while(status.current_count() < 1);
        
        std::cout << "LED Control Client started" << std::endl;
        std::cout << "Connected to server" << std::endl;
    }
    
    void run() 
    {
        // Send initial test requests - turn ALL the LEDs 'ON':
        std::cout << "\n=== Sending Initial Test Requests ===" << std::endl;
        sendRequest(led_control::LedColor::RED, true);
        std::this_thread::sleep_for(500ms);
        sendRequest(led_control::LedColor::GREEN, true);
        std::this_thread::sleep_for(500ms);
        sendRequest(led_control::LedColor::BLUE, true);
        
        // Main loop
        while(running) 
        {
            try 
            {
                // Check for responses
                checkResponses();
                
                // Send random request every 2-5 seconds
                static auto last_request = std::chrono::steady_clock::now();
                auto now = std::chrono::steady_clock::now();
                
                if(now - last_request > 10s) 
                {
                    //sendRandomRequest();    // Turn random LED randomly ON if OFF, or OFF if ON!
                    
                    last_request = now;
                }
                
                std::this_thread::sleep_for(100ms);
                
            } 
            catch(const dds::core::Exception& e) 
            {
                std::cerr << "DDS Exception: " << e.what() << std::endl;
            }
        }
    }
    
    void stop() 
    {
        running = false;
    }
    
    // Method for manual control (can be called from UI or CLI)
    void manualControl(led_control::LedColor color, bool state) 
    {
        sendRequest(color, state);
    }
};



std::atomic<bool> shutdown_flag{false};


void signal_handler(int) 
{
    shutdown_flag = true;
}


int main(int argc, char** argv) 
{
    std::signal(SIGINT, signal_handler);    // Ctrl-C ('kill -5')
    std::signal(SIGTERM, signal_handler);   // 'kill -7' (Ctrl-Q)
    
    try 
    {
        LedClient client(0); // Domain ID 0
        
        // Run client in separate thread
        std::thread client_thread([&client]() {
            client.run();
        });
        
        // Wait for shutdown signal
        while(!shutdown_flag) {
            std::this_thread::sleep_for(100ms);
        }
        
        std::cout << "\nShutting down client..." << std::endl;
        client.stop();
        client_thread.join();
        
        std::cout << "Client stopped successfully" << std::endl;
        
    } catch(const dds::core::Exception& e) {
        std::cerr << "DDS Exception in main: " << e.what() << std::endl;

        return 1;
    } 
    catch(const std::exception& e) 
    {
        std::cerr << "Exception: " << e.what() << std::endl;

        return 1;
    }
    
    return 0;
}

