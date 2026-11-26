MODULE CryptoSHA384;
(** PURPOSE "SHA2-384"; фабрика CryptoSHA512 с начальными значениями SHA-384. *)

	IMPORT Hashes := CryptoHashes, SHA := CryptoSHA512;

	PROCEDURE NewHash* (): Hashes.Hash;
	BEGIN
		RETURN SHA.NewHash384()
	END NewHash;

END CryptoSHA384.
