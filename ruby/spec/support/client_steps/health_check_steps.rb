ClientHealthCheckSteps = RSpec::EM.async_steps do

  def assert_periodic_health_checks_made_when_running(&callback)
    # Verify that 2 health check requests are made
    d1 = add_http_request_expectation health_check_endpoint do |request_params|
      expect(request_params[:firmware_version]).to eq(Smith::FIRMWARE_VERSION)
    end
    
    d2 = add_http_request_expectation health_check_endpoint do |request_params|
      expect(request_params[:firmware_version]).to eq(Smith::FIRMWARE_VERSION)
    end

    when_succeed(d1, d2) { callback.call }

    start_client
  end

  def assert_error_logged_and_health_checks_resume_after_temporary_connection_loss(&callback)

    first_health_check_made = add_http_request_expectation health_check_endpoint do |request_params|
      expect(request_params[:firmware_version]).to eq(Smith::FIRMWARE_VERSION)

      # After getting the first health check, change the server url to simulate unreachable server
      Smith::Settings.server_url = 'http://bad.url'

      expected_entry = Smith::Client::LogMessage.format(
        Smith::Client::LogMessages::POST_REQUEST_URL_UNREACHABLE,
        health_check_endpoint,
        { firmware_version: Smith::FIRMWARE_VERSION }.to_json
      )

      # Next health check request attempt fails
      add_log_subscription(expected_entry) do
        # Reset the server url to simulate server becoming reachable again
        Smith::Settings.server_url = dummy_server.url

        # Client resumes health checks after server becomes reachable
        add_http_request_expectation health_check_endpoint do |request_params|
          expect(request_params[:firmware_version]).to eq(Smith::FIRMWARE_VERSION)
          
          callback.call
        end
      end
    end

    start_client
  end

end
