/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "crypto/cleanse_wrapper.h"
#include "crypto/elliptic_curve_key.h"
#include "ec_commands.h"
#include "fpsensor/fpsensor.h"
#include "fpsensor/fpsensor_auth_commands.h"
#include "fpsensor/fpsensor_auth_crypto.h"
#include "fpsensor/fpsensor_console.h"
#include "fpsensor/fpsensor_crypto.h"
#include "fpsensor/fpsensor_state.h"
#include "openssl/mem.h"
#include "openssl/rand.h"
#include "scoped_fast_cpu.h"
#include "sha256.h"
#include "util.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <utility>
#include <variant>

/* Pointer to the FPMCU's ECDH private key */
static bssl::UniquePtr<EC_KEY> ecdh_key;

/* The GSC pairing key. */
static std::array<uint8_t, FP_PAIRING_KEY_LEN> pairing_key;

/* The session nonce for session key. */
static std::array<uint8_t, FP_CK_SESSION_NONCE_LEN> session_nonce;

static std::array<uint8_t, SHA256_DIGEST_LENGTH> session_key;

/* Current challenge */
static std::array<uint8_t, FP_CHALLENGE_SIZE> challenge;
test_export_static timestamp_t challenge_ctime;

enum ec_error_list check_context_cleared()
{
	for (uint8_t partial : global_context.user_id)
		if (partial != 0)
			return EC_ERROR_ACCESS_DENIED;
	if (global_context.templ_valid != 0)
		return EC_ERROR_ACCESS_DENIED;
	if (global_context.templ_dirty != 0)
		return EC_ERROR_ACCESS_DENIED;
	if (global_context.positive_match_secret_state.template_matched !=
	    FP_NO_SUCH_TEMPLATE)
		return EC_ERROR_ACCESS_DENIED;
	if (global_context.fp_encryption_status & FP_CONTEXT_USER_ID_SET)
		return EC_ERROR_ACCESS_DENIED;
	return EC_SUCCESS;
}

bool fingerprint_auth_enabled()
{
	return global_context.fp_encryption_status &
	       FP_CONTEXT_STATUS_SESSION_ESTABLISHED;
}

static enum ec_status
fp_command_establish_pairing_key_keygen(struct host_cmd_handler_args *args)
{
	auto *r = static_cast<ec_response_fp_establish_pairing_key_keygen *>(
		args->response);

	ScopedFastCpu fast_cpu;

	ecdh_key = generate_elliptic_curve_key();
	if (ecdh_key == nullptr) {
		return EC_RES_UNAVAILABLE;
	}

	std::optional<fp_elliptic_curve_public_key> pubkey =
		create_pubkey_from_ec_key(*ecdh_key);
	if (!pubkey.has_value()) {
		return EC_RES_UNAVAILABLE;
	}

	r->pubkey = pubkey.value();

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_ESTABLISH_PAIRING_KEY_KEYGEN,
		     fp_command_establish_pairing_key_keygen, EC_VER_MASK(0));

static enum ec_status
fp_command_establish_pairing_key_wrap(struct host_cmd_handler_args *args)
{
	const auto *params =
		static_cast<const ec_params_fp_establish_pairing_key_wrap *>(
			args->params);
	auto *r = static_cast<ec_response_fp_establish_pairing_key_wrap *>(
		args->response);

	CleanseWrapper<std::array<uint8_t, FP_PAIRING_KEY_LEN> > new_pairing_key;

	if (ecdh_key == nullptr) {
		return EC_RES_UNAVAILABLE;
	}

	ScopedFastCpu fast_cpu;

	bssl::UniquePtr<EC_KEY> public_key =
		create_ec_key_from_pubkey(params->peers_pubkey);
	if (public_key == nullptr) {
		return EC_RES_UNAVAILABLE;
	}

	/*
	 * The Pairing Key is only used to produce the Session Key.
	 * It's not used as a key for symmetric encryption. It's okay
	 * to not apply KDF in this case.
	 */
	enum ec_error_list ret = generate_ecdh_shared_secret_without_kdf(
		*ecdh_key, *public_key, new_pairing_key);
	if (ret != EC_SUCCESS) {
		return EC_RES_UNAVAILABLE;
	}

	ret = encrypt_pairing_key(FP_AES_KEY_ENC_METADATA_VERSION,
				  r->encrypted_pairing_key.info,
				  new_pairing_key,
				  r->encrypted_pairing_key.data);
	if (ret != EC_SUCCESS) {
		return EC_RES_UNAVAILABLE;
	}

	/* Deallocate the FPMCU's ECDH private key. */
	ecdh_key = nullptr;

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_ESTABLISH_PAIRING_KEY_WRAP,
		     fp_command_establish_pairing_key_wrap, EC_VER_MASK(0));

static enum ec_status
fp_command_load_pairing_key(struct host_cmd_handler_args *args)
{
	const auto *params = static_cast<const ec_params_fp_load_pairing_key *>(
		args->params);

	ScopedFastCpu fast_cpu;

	/* If the context is not cleared, reject this request to prevent leaking
	 * the existing template. */
	enum ec_error_list ret = check_context_cleared();
	if (ret != EC_SUCCESS) {
		CPRINTS("load_pairing_key: Context is not clean");
		return EC_RES_ACCESS_DENIED;
	}

	if (fingerprint_auth_enabled()) {
		CPRINTS("load_pairing_key: Session already established");
		return EC_RES_ACCESS_DENIED;
	}

	ret = decrypt_pairing_key(params->encrypted_pairing_key.info,
				  params->encrypted_pairing_key.data,
				  pairing_key);
	if (ret != EC_SUCCESS) {
		CPRINTS("load_pairing_key: Failed to decrypt pairing key");
		return EC_RES_UNAVAILABLE;
	}

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_LOAD_PAIRING_KEY, fp_command_load_pairing_key,
		     EC_VER_MASK(0));

__maybe_unused test_export_static void reset_session(void)
{
	OPENSSL_cleanse(session_nonce.data(), session_nonce.size());
	OPENSSL_cleanse(session_key.data(), session_key.size());
	OPENSSL_cleanse(global_context.tpm_seed.data(),
			global_context.tpm_seed.size());
	global_context.fp_encryption_status &= ~(
		FP_CONTEXT_SESSION_NONCE_SET |
		FP_CONTEXT_STATUS_SESSION_ESTABLISHED | FP_ENC_STATUS_SEED_SET);
}

static enum ec_status
fp_command_generate_nonce(struct host_cmd_handler_args *args)
{
	auto *r = static_cast<ec_response_fp_generate_nonce *>(args->response);

	ScopedFastCpu fast_cpu;

	if (fingerprint_auth_enabled()) {
		/* Invalidate the existing context and templates to prevent
		 * leaking the existing template. */
		fp_reset_context();
	}

	RAND_bytes(session_nonce.data(), session_nonce.size());

	std::ranges::copy(session_nonce, r->nonce);

	global_context.fp_encryption_status |= FP_CONTEXT_SESSION_NONCE_SET;

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_GENERATE_NONCE, fp_command_generate_nonce,
		     EC_VER_MASK(0));

static enum ec_status
fp_command_establish_session(struct host_cmd_handler_args *args)
{
	const auto *p = static_cast<const ec_params_fp_establish_session *>(
		args->params);
	static constexpr uint8_t tpm_seed_aad[] = { 't', 'p', 'm', '_',
						    's', 'e', 'e', 'd' };
	constexpr auto aad = std::span{ tpm_seed_aad };

	if (!(global_context.fp_encryption_status &
	      FP_CONTEXT_SESSION_NONCE_SET)) {
		CPRINTS("No existing session nonce");
		return EC_RES_ACCESS_DENIED;
	}

	ScopedFastCpu fast_cpu;

	enum ec_error_list ret = generate_session_key(
		session_nonce, p->peer_nonce, pairing_key, session_key);
	if (ret != EC_SUCCESS) {
		return EC_RES_INVALID_PARAM;
	}

	static_assert(sizeof(global_context.tpm_seed) ==
		      sizeof(p->enc_tpm_seed));
	CleanseWrapper<std::array<uint8_t, sizeof(global_context.tpm_seed)> >
		tpm_seed;

	ret = decrypt_data_with_session_key(session_key, p->enc_tpm_seed,
					    tpm_seed, p->nonce, p->tag, aad);
	if (ret != EC_SUCCESS) {
		return EC_RES_ERROR;
	}

	/* Set the TPM Seed. */
	std::ranges::copy(tpm_seed, global_context.tpm_seed.begin());
	global_context.fp_encryption_status |= FP_ENC_STATUS_SEED_SET;
	global_context.fp_encryption_status &= ~FP_CONTEXT_SESSION_NONCE_SET;
	global_context.fp_encryption_status |=
		FP_CONTEXT_STATUS_SESSION_ESTABLISHED;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_ESTABLISH_SESSION, fp_command_establish_session,
		     EC_VER_MASK(0));

static enum ec_status
fp_cmd_generate_challenge(struct host_cmd_handler_args *args)
{
	auto *r = static_cast<ec_response_fp_generate_challenge *>(
		args->response);

	/* The Session Key is used to sign messages. Let's make sure
	 * it's available. */
	if (!fingerprint_auth_enabled()) {
		return EC_RES_ACCESS_DENIED;
	}

	ScopedFastCpu fast_cpu;

	RAND_bytes(challenge.data(), challenge.size());
	std::ranges::copy(challenge, r->challenge);

	timestamp_t now = get_time();
	challenge_ctime.val = now.val;

	global_context.fp_encryption_status |= FP_AUTH_CHALLENGE_SET;

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_GENERATE_CHALLENGE, fp_cmd_generate_challenge,
		     EC_VER_MASK(0));

enum ec_error_list
validate_request(std::span<const uint8_t> context,
		 std::span<const uint8_t> operation,
		 std::span<const uint8_t, SHA256_DIGEST_LENGTH> mac)
{
	/* We expect the message to come from Fingerguard. */
	static constexpr uint8_t sender_str[] = {
		'f', 'i', 'n', 'g', 'e', 'r', '_', 'g', 'u', 'a', 'r', 'd'
	};
	static constexpr std::span sender = sender_str;
	std::array<uint8_t, SHA256_DIGEST_LENGTH> computed_mac{};

	/* Make sure new challenge was generated. */
	if (!(global_context.fp_encryption_status & FP_AUTH_CHALLENGE_SET)) {
		return EC_ERROR_ACCESS_DENIED;
	}

	/* Remove the bit so the challenge is not reused. */
	global_context.fp_encryption_status &= ~FP_AUTH_CHALLENGE_SET;

	/* Make sure the challenge has not expired. */
	timestamp_t now = get_time();
	if (now.val > challenge_ctime.val + (5 * SECOND)) {
		return EC_ERROR_TIMEOUT;
	}

	/* Compute expected signature. */
	if (compute_message_signature(session_key, context, sender, operation,
				      challenge, computed_mac) != EC_SUCCESS) {
		return EC_ERROR_INVAL;
	}

	/* Compare computed signature with received one. */
	static_assert(mac.size() == computed_mac.size());
	if (CRYPTO_memcmp(mac.data(), computed_mac.data(), mac.size())) {
		return EC_ERROR_ACCESS_DENIED;
	}

	return EC_SUCCESS;
}

enum ec_error_list
sign_message(std::span<const uint8_t> context,
	     std::span<const uint8_t> operation,
	     std::span<const uint8_t, FP_CHALLENGE_SIZE> peer_challenge,
	     std::span<uint8_t, SHA256_DIGEST_LENGTH> output)
{
	static constexpr uint8_t sender_str[] = { 'f', 'p', 'm', 'c', 'u' };
	static constexpr std::span sender = sender_str;

	/* The Session Key is used to sign messages. Let's make sure
	 * it's available. */
	if (!fingerprint_auth_enabled()) {
		return EC_ERROR_ACCESS_DENIED;
	}

	if (compute_message_signature(session_key, context, sender, operation,
				      peer_challenge, output) != EC_SUCCESS) {
		return EC_ERROR_INVAL;
	}

	return EC_SUCCESS;
}
