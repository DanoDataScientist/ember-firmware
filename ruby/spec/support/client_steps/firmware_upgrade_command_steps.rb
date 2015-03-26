module Smith
  module Client
    FirmwareUpgradeCommandSteps = RSpec::EM.async_steps do

      def assert_firmware_upgrade_command_handled_when_firmware_upgrade_command_received_when_upgrade_succeeds(url, &callback)
        d1 = add_http_request_expectation acknowledge_endpoint(command_context) do |request_params|
          expect(request_params[:data][:command]).to eq(FIRMWARE_UPGRADE_COMMAND)
          expect(request_params[:data][:state]).to eq(Command::RECEIVED_ACK)
        end
        
        d2 = add_http_request_expectation acknowledge_endpoint(command_context) do |request_params|
          expect(request_params[:data][:command]).to eq(FIRMWARE_UPGRADE_COMMAND)
          expect(request_params[:data][:state]).to eq(Command::COMPLETED_ACK)

          # Check that the upgrade was performed
          expect(File.file?(File.join(firmware_dir, 'smith-0.0.2.img'))).to eq(true)
        end

        when_succeed(d1, d2) { callback.call }

        dummy_server.post_command(command: FIRMWARE_UPGRADE_COMMAND, task_id: test_task_id, package_url: url)
      end

      def assert_failure_acknowledgement_sent_when_firmware_upgrade_command_received_when_upgrade_fails(&callback)
        d1 = add_http_request_expectation acknowledge_endpoint(command_context) do |request_params|
          expect(request_params[:data][:command]).to eq(FIRMWARE_UPGRADE_COMMAND)
          expect(request_params[:data][:state]).to eq(Command::RECEIVED_ACK)
        end
        
        d2 = add_http_request_expectation acknowledge_endpoint(command_context) do |request_params|
          expect(request_params[:data][:state]).to eq(Command::FAILED_ACK)
          expect(request_params[:data][:command]).to eq(FIRMWARE_UPGRADE_COMMAND)
          expect(request_params[:data][:message]).to match_log_message(
            LogMessages::EXCEPTION_BRIEF,
            Config::Firmware::UpgradeError.new('')
          )
        end

        when_succeed(d1, d2) { callback.call }

        dummy_server.post_command(
          command: FIRMWARE_UPGRADE_COMMAND,
          task_id: test_task_id,
          package_url: dummy_server.invalid_firmware_upgrade_package_url
        )
      end

      def assert_failure_acknowledgement_sent_when_firmware_upgrade_command_received_when_download_fails(&callback)
        d1 = add_http_request_expectation acknowledge_endpoint(command_context) do |request_params|
          expect(request_params[:data][:command]).to eq(FIRMWARE_UPGRADE_COMMAND)
          expect(request_params[:data][:state]).to eq(Command::RECEIVED_ACK)
        end
        
        d2 = add_http_request_expectation acknowledge_endpoint(command_context) do |request_params|
          expect(request_params[:data][:state]).to eq(Command::FAILED_ACK)
          expect(request_params[:data][:command]).to eq(FIRMWARE_UPGRADE_COMMAND)
          expect(request_params[:data][:message]).to match_log_message(
            LogMessages::FIRMWARE_DOWNLOAD_ERROR,
            dummy_server.invalid_url
          )
        end

        when_succeed(d1, d2) { callback.call }
        
        dummy_server.post_command(
          command: FIRMWARE_UPGRADE_COMMAND,
          task_id: test_task_id,
          package_url: dummy_server.invalid_url
        )
      end

    end
  end
end
