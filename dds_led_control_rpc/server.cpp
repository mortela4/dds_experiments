#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include <csignal>

/* Include the C++ DDS API. */
#include "dds/dds.hpp"
#include "dds/dds.h"

#include "LedControl.hpp"


using namespace std::chrono_literals;


class LedServer 
{
private:
    dds::domain::DomainParticipant participant;
    dds::topic::Topic<led_control::LedRequest> request_topic;
    dds::topic::Topic<led_control::LedResponse> response_topic;
    dds::sub::Subscriber subscriber;
    dds::pub::Publisher publisher;
    dds::sub::DataReader<led_control::LedRequest> request_reader;
    dds::pub::DataWriter<led_control::LedResponse> response_writer;
    
    std::atomic<bool> running{true};
    
    // Simulated LED states
    bool led_states[3] = {false, false, false}; // RED, GREEN, BLUE
    
    const char* colorToString(led_control::LedColor color) 
    {
        switch(color) {
            case led_control::LedColor::RED: return "RED";
            case led_control::LedColor::GREEN: return "GREEN";
            case led_control::LedColor::BLUE: return "BLUE";
            default: return "UNKNOWN";
        }
    }
    
    void processRequest(const led_control::LedRequest& request) 
    {
        std::cout << "Received request: "
                  << colorToString(request.color()) 
                  << " -> " << (request.state() ? "ON" : "OFF")
                  << " (ID: " << request.request_id() << ")" << std::endl;
        
        // Simulate hardware control
        int color_index = static_cast<int>(request.color());
        led_states[color_index] = request.state();
        
        // Simulate some processing delay
        std::this_thread::sleep_for(10ms);
        
        // Prepare response
        led_control::LedResponse response;
        response.success(true);
        response.message("LED control successful");
        response.color(request.color());
        response.state(request.state());
        response.request_id(request.request_id());
        
        // Send response
        response_writer.write(response);
        
        std::cout << "Sent response for request ID: " 
                  << request.request_id() << std::endl;
    }
    
    void simulateHardwareControl() 
    {
        std::cout << "\nCurrent LED States:" << std::endl;
        std::cout << "RED: " << (led_states[0] ? "ON" : "OFF") << std::endl;
        std::cout << "GREEN: " << (led_states[1] ? "ON" : "OFF") << std::endl;
        std::cout << "BLUE: " << (led_states[2] ? "ON" : "OFF") << std::endl;
    }

public:
    LedServer(int domain_id = 0) 
        : participant(domain_id),
          request_topic(participant, "led_control_requests"),
          response_topic(participant, "led_control_responses"),
          subscriber(participant),
          publisher(participant),
          request_reader(subscriber, request_topic),
          response_writer(publisher, response_topic) {
        
        std::cout << "LED Control Server started" << std::endl;
        std::cout << "Listening for requests on topic: led_control_requests" << std::endl;
        std::cout << "Sending responses on topic: led_control_responses" << std::endl;
    }
    
    void run() 
    {
        dds::sub::cond::ReadCondition read_cond(
            request_reader,
            dds::sub::status::DataState::any());
        
        dds::core::cond::WaitSet  waitset;
        waitset += read_cond;
        
        while (running) 
        {
            try {
                auto samples = request_reader.select()
                    .state(dds::sub::status::DataState::new_data())
                    .take();
                
                for (const auto& sample : samples) 
                {
                    if(sample.info().valid()) 
                    {
                        processRequest(sample.data());
                    }
                }
                
                // Periodically show current state
                static auto last_display = std::chrono::steady_clock::now();
                auto now = std::chrono::steady_clock::now();
                if(now - last_display > 5s) {
                    simulateHardwareControl();
                    last_display = now;
                }
                
                // Wait for next request with timeout
                auto conditions = waitset.wait(dds::core::Duration::from_secs(1.0));
                
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
    
    try {
        LedServer server(0); // Domain ID 0
        
        // Run server in separate thread
        std::thread server_thread([&server]() 
        {
            server.run();
        });
        
        // Wait for shutdown signal
        while(!shutdown_flag) 
        {
            std::this_thread::sleep_for(100ms);
        }
        
        std::cout << "\nShutting down server..." << std::endl;
        server.stop();
        server_thread.join();
        
        std::cout << "Server stopped successfully" << std::endl;
        
    } 
    catch(const dds::core::Exception& e) 
    {
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

