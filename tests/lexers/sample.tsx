type Props = { count: number };

export function Counter({ count }: Props) {
  return <button disabled={count === 0}>Count: {count}</button>;
}
