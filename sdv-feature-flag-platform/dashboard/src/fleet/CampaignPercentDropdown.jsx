import { useEffect, useRef, useState } from 'react';

export default function CampaignPercentDropdown({
  label,
  value,
  options,
  onChange,
  disabled,
  placeholder = 'Select…',
  formatOption = (o) => o,
  optionKey = (o) => o,
}) {
  const [open, setOpen] = useState(false);
  const rootRef = useRef(null);

  useEffect(() => {
    if (!open) return undefined;
    const onDoc = (e) => {
      if (rootRef.current && !rootRef.current.contains(e.target)) {
        setOpen(false);
      }
    };
    document.addEventListener('mousedown', onDoc);
    return () => document.removeEventListener('mousedown', onDoc);
  }, [open]);

  const display = value ? formatOption(value) : placeholder;

  return (
    <div
      className={`campaign-percent-dropdown${disabled ? ' campaign-percent-dropdown-disabled' : ''}`}
      ref={rootRef}
    >
      <span className="campaign-percent-field-label tiny subtle">{label}</span>
      <button
        type="button"
        className={`campaign-percent-dropdown-btn${open ? ' campaign-percent-dropdown-btn-open' : ''}${value ? ' campaign-percent-dropdown-btn-filled' : ''}`}
        disabled={disabled}
        aria-expanded={open}
        onClick={() => !disabled && setOpen((v) => !v)}
      >
        {display}
        <span className="campaign-percent-dropdown-caret" aria-hidden>
          ▾
        </span>
      </button>
      {open && !disabled ? (
        <ul className="campaign-percent-dropdown-menu" role="listbox">
          {options.length ? (
            options.map((opt) => {
              const key = optionKey(opt);
              const active = optionKey(value) === key;
              return (
                <li key={key}>
                  <button
                    type="button"
                    role="option"
                    aria-selected={active}
                    className={`campaign-percent-dropdown-item${active ? ' campaign-percent-dropdown-item-active' : ''}`}
                    onClick={() => {
                      onChange(opt);
                      setOpen(false);
                    }}
                  >
                    {formatOption(opt)}
                  </button>
                </li>
              );
            })
          ) : (
            <li className="tiny subtle campaign-percent-dropdown-empty">
              No options (local image required)
            </li>
          )}
        </ul>
      ) : null}
    </div>
  );
}
