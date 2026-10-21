import os
import json
from pathlib import Path
from typing import List, Dict, Any

DATA_DIR = Path(os.getenv("DATA_DIR", "/app/data"))
MAX_CHUNKS = int(os.getenv("MAX_INGESTION_CHUNKS", "1000"))

def fetch_legalbench() -> List[Dict[str, Any]]:
    """Download a maintained legal passage corpus from HuggingFace datasets.

    Uses ``isaacus/legal-rag-bench`` (corpus subset): ~4,876 passages from the
    Victorian Criminal Charge Book, published as parquet. The legacy
    ``nguha/legalbench`` dataset is no longer used: its loading script
    references data files that are unavailable and it has no RAG passage
    corpus (its 162 configs are classification/QA tasks).
    """
    print("Downloading legal corpus from HuggingFace...")
    try:
        from datasets import load_dataset

        dataset = load_dataset(
            "isaacus/legal-rag-bench",
            "corpus",
            split="test",
            trust_remote_code=True,
        )
        print(f"Loaded legal-rag-bench corpus: {len(dataset)} examples")

        documents = []
        for example in dataset:
            text = example.get("text", "") or example.get("passage", "") or example.get("context", "")
            if text:
                documents.append({
                    "text": text,
                    "metadata": {
                        "source": "legal-rag-bench",
                        "title": example.get("title", ""),
                        "id": str(example.get("id", "")),
                        "jurisdiction": "australia",
                    }
                })

        print(f"Loaded {len(documents)} documents from legal-rag-bench")
        return documents[:MAX_CHUNKS]

    except Exception as e:
        print(f"Failed to load legal-rag-bench: {e}")
        raise

def fetch_local_fallback() -> List[Dict[str, Any]]:
    """Fallback: create sample legal documents for local testing."""
    print("Using local fallback data...")
    samples = [
        {
            "text": "Section 337 of the Tariff Act of 1930 prohibits unfair methods of competition and unfair acts in the importation of articles into the United States. This includes patent infringement, trademark infringement, and misappropriation of trade secrets.",
            "metadata": {"source": "USC_19_1337", "jurisdiction": "federal", "topic": "trade_secrets"}
        },
        {
            "text": "Under California Civil Code Section 3426 et seq. (CUTSA), a trade secret is information that derives independent economic value from not being generally known and is subject to reasonable efforts to maintain secrecy. Misappropriation includes acquisition by improper means or disclosure without consent.",
            "metadata": {"source": "CA_Civ_3426", "jurisdiction": "california", "topic": "trade_secrets"}
        },
        {
            "text": "The Defend Trade Secrets Act of 2016 (18 U.S.C. 1836 et seq.) creates a federal civil cause of action for trade secret misappropriation. It provides for injunctive relief, damages, and in exceptional circumstances, ex parte seizure orders.",
            "metadata": {"source": "DTSA_2016", "jurisdiction": "federal", "topic": "trade_secrets"}
        },
        {
            "text": "In contract law, a breach occurs when a party fails to perform their obligations under the contract. Material breach allows the non-breaching party to terminate the contract and seek damages. Minor breach only allows damages.",
            "metadata": {"source": "contract_law_basics", "jurisdiction": "general", "topic": "contracts"}
        },
        {
            "text": "The statute of limitations for breach of written contract in California is 4 years (CCP 337). For oral contracts, it is 2 years (CCP 339). The clock starts when the breach occurs or is discovered.",
            "metadata": {"source": "CA_CCP_337_339", "jurisdiction": "california", "topic": "statute_of_limitations"}
        },
        {
            "text": "Negligence requires four elements: (1) duty of care, (2) breach of duty, (3) causation (actual and proximate), and (4) damages. The standard of care is that of a reasonable person under similar circumstances.",
            "metadata": {"source": "negligence_elements", "jurisdiction": "general", "topic": "torts"}
        },
        {
            "text": "Under New York law, the statute of limitations for legal malpractice is 3 years from the date of the malpractice (CPLR 214(6)). The continuous representation doctrine may toll the statute.",
            "metadata": {"source": "NY_CPLR_214", "jurisdiction": "new_york", "topic": "statute_of_limitations"}
        },
        {
            "text": "Attorney-client privilege protects confidential communications between attorney and client made for the purpose of obtaining legal advice. The privilege belongs to the client and can only be waived by the client.",
            "metadata": {"source": "attorney_client_privilege", "jurisdiction": "general", "topic": "evidence"}
        },
        {
            "text": "The work product doctrine (FRCP 26(b)(3)) protects materials prepared in anticipation of litigation by or for a party's representative. It provides broader protection than attorney-client privilege for mental impressions.",
            "metadata": {"source": "work_product_doctrine", "jurisdiction": "federal", "topic": "evidence"}
        },
        {
            "text": "In federal court, summary judgment is appropriate when there is no genuine dispute as to any material fact and the movant is entitled to judgment as a matter of law (FRCP 56). The court views evidence in light most favorable to non-movant.",
            "metadata": {"source": "FRCP_56", "jurisdiction": "federal", "topic": "civil_procedure"}
        }
    ]
    # Replicate the samples to reach MAX_CHUNKS, but make each copy unique by
    # appending a reference marker to the text. Chunk ids are UUIDv5(text), so
    # identical text would produce duplicate ids and break batch_insert's
    # ON CONFLICT DO UPDATE (which cannot affect the same row twice).
    docs = []
    for i in range(MAX_CHUNKS):
        sample = samples[i % len(samples)]
        doc = dict(sample)
        doc["text"] = sample["text"] + f" [reference {i}]"
        docs.append(doc)
    return docs

def main():
    DATA_DIR.mkdir(parents=True, exist_ok=True)

    try:
        documents = fetch_legalbench()
    except Exception as e:
        print(f"Failed to load LegalBench: {e}. Using fallback.")
        documents = fetch_local_fallback()

    output_file = DATA_DIR / "raw_documents.json"
    with open(output_file, "w", encoding="utf-8") as f:
        json.dump(documents, f, indent=2)

    print(f"Saved {len(documents)} documents to {output_file}")

if __name__ == "__main__":
    main()